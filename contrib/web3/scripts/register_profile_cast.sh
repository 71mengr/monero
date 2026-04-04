#!/usr/bin/env bash
set -euo pipefail

# Registers/updates a Monero daemon Web3 network profile on an EVM chain.
# Requirements:
# - jq
# - cast (Foundry)
# - MONERO_RPC_URL (default: http://127.0.0.1:18081)
# - REGISTRY_ADDRESS
# - RPC_URL (EVM node URL)
# - PRIVATE_KEY

MONERO_RPC_URL="${MONERO_RPC_URL:-http://127.0.0.1:18081}"
: "${REGISTRY_ADDRESS:?REGISTRY_ADDRESS is required}"
: "${RPC_URL:?RPC_URL is required}"
: "${PRIVATE_KEY:?PRIVATE_KEY is required}"

NETWORK_JSON="$(curl -s "${MONERO_RPC_URL}/get_web3_network")"

NETWORK="$(jq -r '.network' <<<"${NETWORK_JSON}")"
CHAIN_ID_HEX="$(jq -r '.chain_id_hex' <<<"${NETWORK_JSON}")"
NETWORK_ID="$(jq -r '.network_id' <<<"${NETWORK_JSON}")"
WEB3_UNIQUE_ID="$(jq -r '.web3_unique_id' <<<"${NETWORK_JSON}")"

DAEMON_ENDPOINT="${DAEMON_ENDPOINT:-${MONERO_RPC_URL}}"
DAEMON_VERSION="${DAEMON_VERSION:-monero-daemon}"

echo "Registering profile"
echo "  network       : ${NETWORK}"
echo "  chain_id_hex  : ${CHAIN_ID_HEX}"
echo "  network_id    : ${NETWORK_ID}"
echo "  web3_unique_id: ${WEB3_UNIQUE_ID}"

cast send "${REGISTRY_ADDRESS}" \
  "upsertProfile(string,string,string,string,string,string)" \
  "${NETWORK}" \
  "${CHAIN_ID_HEX}" \
  "${NETWORK_ID}" \
  "${WEB3_UNIQUE_ID}" \
  "${DAEMON_ENDPOINT}" \
  "${DAEMON_VERSION}" \
  --rpc-url "${RPC_URL}" \
  --private-key "${PRIVATE_KEY}"
