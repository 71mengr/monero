// Copyright (c) 2014-2022, The Monero Project
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
//
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
//
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

#include "checkpoints.h"

#include "common/dns_utils.h"
#include "string_tools.h"
#include "net/http_client.h"
#include "net/parse.h"
#include "storages/portable_storage_template_helper.h" // epee json include
#include "serialization/keyvalue_serialization.h"
#include <boost/system/error_code.hpp>
#include <boost/filesystem.hpp>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <sstream>
#include <vector>

using namespace epee;

#undef MONERO_DEFAULT_LOG_CATEGORY
#define MONERO_DEFAULT_LOG_CATEGORY "checkpoints"

namespace cryptonote
{
  namespace
  {
    static bool parse_checkpoint_line(const std::string &line, uint64_t &height, std::string &hash)
    {
      const auto begin = line.find_first_not_of(" \t\r");
      if (begin == std::string::npos || line[begin] == '#')
        return false;

      const auto end = line.find_last_not_of(" \t\r");
      const std::string trimmed = line.substr(begin, end - begin + 1);

      size_t separator = trimmed.find(',');
      if (separator == std::string::npos)
        separator = trimmed.find(':');
      if (separator == std::string::npos)
      {
        for (size_t i = 0; i < trimmed.size(); ++i)
        {
          if (std::isspace(static_cast<unsigned char>(trimmed[i])))
          {
            separator = i;
            break;
          }
        }
      }
      if (separator == std::string::npos)
        return false;

      const auto hash_begin = trimmed.find_first_not_of(" \t", separator + 1);
      if (hash_begin == std::string::npos)
        return false;
      const std::string height_str = trimmed.substr(0, separator);
      std::string hash_str = trimmed.substr(hash_begin);
      hash_str.erase(std::find_if(hash_str.rbegin(), hash_str.rend(),
        [](unsigned char c){ return !std::isspace(c); }).base(), hash_str.end());

      std::stringstream ss(height_str);
      if (!(ss >> height) || !ss.eof())
        return false;

      crypto::hash h;
      if (!epee::string_tools::hex_to_pod(hash_str, h))
        return false;

      std::transform(hash_str.begin(), hash_str.end(), hash_str.begin(),
        [](unsigned char c){ return std::tolower(c); });
      hash = std::move(hash_str);
      return true;
    }

    static bool load_checkpoints_from_text(const std::string &text, std::vector<std::string> &records)
    {
      std::istringstream input(text);
      std::string line;
      while (std::getline(input, line))
      {
        uint64_t height = 0;
        std::string hash;
        if (!parse_checkpoint_line(line, height, hash))
          continue;
        records.emplace_back(std::to_string(height) + ":" + hash);
      }
      return !records.empty();
    }

    static bool load_checkpoints_from_custom_source(const std::string &source, std::vector<std::string> &records)
    {
      std::string body;
      const bool has_scheme = source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
      const bool file_exists = boost::filesystem::exists(source);
      if (has_scheme || !file_exists)
      {
        std::string url = source;
        if (!has_scheme)
        {
          url = "http://" + source;
          if (url.find('/', strlen("http://")) == std::string::npos)
            url += "/dns.txt";
        }

        epee::net_utils::http::url_content u_c;
        if (!epee::net_utils::parse_url(url, u_c) || u_c.host.empty())
        {
          MERROR("Failed to parse custom checkpoints source URL: " << source);
          return false;
        }

        const auto ssl = u_c.schema == "https" ?
          epee::net_utils::ssl_support_t::e_ssl_support_enabled :
          epee::net_utils::ssl_support_t::e_ssl_support_disabled;
        const uint16_t port = u_c.port ? u_c.port : (ssl == epee::net_utils::ssl_support_t::e_ssl_support_enabled ? 443 : 80);

        epee::net_utils::http::http_simple_client client;
        client.set_server(u_c.host, std::to_string(port), boost::none, ssl);
        if (!client.connect(std::chrono::seconds(30)))
        {
          MERROR("Failed to connect to custom checkpoints source URL: " << url);
          return false;
        }

        const epee::net_utils::http::http_response_info *info = nullptr;
        if (!client.invoke_get(u_c.uri.empty() ? "/" : u_c.uri, std::chrono::seconds(30), "", &info) || !info)
        {
          MERROR("Failed to fetch custom checkpoints source URL: " << url);
          client.disconnect();
          return false;
        }

        if (info->m_response_code != 200)
        {
          MERROR("Custom checkpoints source returned HTTP status " << info->m_response_code << " from " << url);
          client.disconnect();
          return false;
        }

        body = info->m_body;
        client.disconnect();
      }
      else
      {
        std::ifstream file(source);
        if (!file.good())
        {
          MERROR("Failed to open custom checkpoints source file: " << source);
          return false;
        }
        std::ostringstream content;
        content << file.rdbuf();
        body = content.str();
      }

      if (!load_checkpoints_from_text(body, records))
      {
        MERROR("Custom checkpoints source has no valid checkpoint entries: " << source);
        return false;
      }
      return true;
    }
  }

  /**
   * @brief struct for loading a checkpoint from json
   */
  struct t_hashline
  {
    uint64_t height; //!< the height of the checkpoint
    std::string hash; //!< the hash for the checkpoint
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(height)
          KV_SERIALIZE(hash)
        END_KV_SERIALIZE_MAP()
  };

  /**
   * @brief struct for loading many checkpoints from json
   */
  struct t_hash_json {
    std::vector<t_hashline> hashlines; //!< the checkpoint lines from the file
        BEGIN_KV_SERIALIZE_MAP()
          KV_SERIALIZE(hashlines)
        END_KV_SERIALIZE_MAP()
  };

  //---------------------------------------------------------------------------
  checkpoints::checkpoints()
  {
  }
  //---------------------------------------------------------------------------
  bool checkpoints::add_checkpoint(uint64_t height, const std::string& hash_str, const std::string& difficulty_str)
  {
    crypto::hash h = crypto::null_hash;
    bool r = epee::string_tools::hex_to_pod(hash_str, h);
    CHECK_AND_ASSERT_MES(r, false, "Failed to parse checkpoint hash string into binary representation!");

    // return false if adding at a height we already have AND the hash is different
    if (m_points.count(height))
    {
      CHECK_AND_ASSERT_MES(h == m_points[height], false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
    }
    m_points[height] = h;
    if (!difficulty_str.empty())
    {
      try
      {
        difficulty_type difficulty(difficulty_str);
        if (m_difficulty_points.count(height))
        {
          CHECK_AND_ASSERT_MES(difficulty == m_difficulty_points[height], false, "Difficulty checkpoint at given height already exists, and difficulty for new checkpoint was different!");
        }
        m_difficulty_points[height] = difficulty;
      }
      catch (...)
      {
        LOG_ERROR("Failed to parse difficulty checkpoint: " << difficulty_str);
        return false;
      }
    }
    return true;
  }
  //---------------------------------------------------------------------------
  bool checkpoints::is_in_checkpoint_zone(uint64_t height) const
  {
    return !m_points.empty() && (height <= (--m_points.end())->first);
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h, bool& is_a_checkpoint) const
  {
    auto it = m_points.find(height);
    is_a_checkpoint = it != m_points.end();
    if(!is_a_checkpoint)
      return true;

    if(it->second == h)
    {
      MINFO("CHECKPOINT PASSED FOR HEIGHT " << height << " " << h);
      return true;
    }else
    {
      MWARNING("CHECKPOINT FAILED FOR HEIGHT " << height << ". EXPECTED HASH: " << it->second << ", FETCHED HASH: " << h);
      return false;
    }
  }
  //---------------------------------------------------------------------------
  bool checkpoints::check_block(uint64_t height, const crypto::hash& h) const
  {
    bool ignored;
    return check_block(height, h, ignored);
  }
  //---------------------------------------------------------------------------
  //FIXME: is this the desired behavior?
  bool checkpoints::is_alternative_block_allowed(uint64_t blockchain_height, uint64_t block_height) const
  {
    if (0 == block_height)
      return false;

    auto it = m_points.upper_bound(blockchain_height);
    // Is blockchain_height before the first checkpoint?
    if (it == m_points.begin())
      return true;

    --it;
    uint64_t checkpoint_height = it->first;
    return checkpoint_height < block_height;
  }
  //---------------------------------------------------------------------------
  uint64_t checkpoints::get_max_height() const
  {
    if (m_points.empty())
      return 0;
    return m_points.rbegin()->first;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, crypto::hash>& checkpoints::get_points() const
  {
    return m_points;
  }
  //---------------------------------------------------------------------------
  const std::map<uint64_t, difficulty_type>& checkpoints::get_difficulty_points() const
  {
    return m_difficulty_points;
  }

  bool checkpoints::check_for_conflicts(const checkpoints& other) const
  {
    for (auto& pt : other.get_points())
    {
      if (m_points.count(pt.first))
      {
        CHECK_AND_ASSERT_MES(pt.second == m_points.at(pt.first), false, "Checkpoint at given height already exists, and hash for new checkpoint was different!");
      }
    }
    return true;
  }

  bool checkpoints::init_default_checkpoints(network_type nettype)
  {
    if (nettype == TESTNET)
    {
      ADD_CHECKPOINT2(0, "48ca7cd3c8de5b6a4d53d2861fbdaedca141553559f9be9520068053cda8430b", "0x1");
      return true;
    }
    if (nettype == STAGENET)
    {
      ADD_CHECKPOINT2(0, "76ee3cc98646292206cd3e86f74d88b4dcc1d937088645e9b0cbca84b7ce74eb", "0x1");
      return true;
    }
    ADD_CHECKPOINT2(0,"4fa21c210a79a506d0ea9421ce2fc70b08654ec052ca62568a9b25a4082efeff", "0x1");
    return true;
  }

  bool checkpoints::load_checkpoints_from_json(const std::string &json_hashfile_fullpath)
  {
    boost::system::error_code errcode;
    if (! (boost::filesystem::exists(json_hashfile_fullpath, errcode)))
    {
      LOG_PRINT_L1("Blockchain checkpoints file not found");
      return true;
    }

    LOG_PRINT_L1("Adding checkpoints from blockchain hashfile");

    uint64_t prev_max_height = get_max_height();
    LOG_PRINT_L1("Hard-coded max checkpoint height is " << prev_max_height);
    t_hash_json hashes;
    if (!epee::serialization::load_t_from_json_file(hashes, json_hashfile_fullpath))
    {
      MERROR("Error loading checkpoints from " << json_hashfile_fullpath);
      return false;
    }
    for (std::vector<t_hashline>::const_iterator it = hashes.hashlines.begin(); it != hashes.hashlines.end(); )
    {
      uint64_t height;
      height = it->height;
      if (height <= prev_max_height) {
	LOG_PRINT_L1("ignoring checkpoint height " << height);
      } else {
	std::string blockhash = it->hash;
	LOG_PRINT_L1("Adding checkpoint height " << height << ", hash=" << blockhash);
	ADD_CHECKPOINT(height, blockhash);
      }
      ++it;
    }

    return true;
  }

  bool checkpoints::load_checkpoints_from_dns(network_type nettype)
  {
    std::vector<std::string> records;
    const char *custom_source = std::getenv("MONERO_DNS_CHECKPOINTS_SOURCE");
    if (custom_source && custom_source[0] != '\0')
    {
      MINFO("Loading checkpoints from custom source: " << custom_source);
      if (!load_checkpoints_from_custom_source(custom_source, records))
        return false;
    }
    else
    {
      static const std::vector<std::string> dns_urls = { "checkpoints.moneropulse.se"
                               , "checkpoints.moneropulse.org"
                               , "checkpoints.moneropulse.net"
                               , "checkpoints.moneropulse.co"
      };

      static const std::vector<std::string> testnet_dns_urls = { "testpoints.moneropulse.se"
                                   , "testpoints.moneropulse.org"
                                   , "testpoints.moneropulse.net"
                                   , "testpoints.moneropulse.co"
      };

    static const std::vector<std::string> stagenet_dns_urls = { "stagenetpoints.moneropulse.se"
                   , "stagenetpoints.moneropulse.org"
                   , "stagenetpoints.moneropulse.net"
                   , "stagenetpoints.moneropulse.co"
    };

      if (!tools::dns_utils::load_txt_records_from_dns(records, nettype == TESTNET ? testnet_dns_urls : nettype == STAGENET ? stagenet_dns_urls : dns_urls))
        return true; // why true ?
    }

    for (const auto& record : records)
    {
      auto pos = record.find(":");
      if (pos != std::string::npos)
      {
        uint64_t height;
        crypto::hash hash;

        // parse the first part as uint64_t,
        // if this fails move on to the next record
        std::stringstream ss(record.substr(0, pos));
        if (!(ss >> height))
        {
    continue;
        }

        // parse the second part as crypto::hash,
        // if this fails move on to the next record
        std::string hashStr = record.substr(pos + 1);
        if (!epee::string_tools::hex_to_pod(hashStr, hash))
        {
    continue;
        }

        ADD_CHECKPOINT(height, hashStr);
      }
    }
    return true;
  }

  bool checkpoints::load_new_checkpoints(const std::string &json_hashfile_fullpath, network_type nettype, bool dns)
  {
    bool result;

    result = load_checkpoints_from_json(json_hashfile_fullpath);
    if (dns)
    {
      result &= load_checkpoints_from_dns(nettype);
    }

    return result;
  }
}
