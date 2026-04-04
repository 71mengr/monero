// SPDX-License-Identifier: BSD-3-Clause
pragma solidity ^0.8.21;

/// @title MoneroNetworkRegistry
/// @notice Ethereum-compatible registry for Monero daemon Web3 identity records.
/// @dev Designed around the daemon RPC additions:
///      - network          (mainnet|testnet|stagenet|fakechain)
///      - chain_id_hex     (for example, 0x12)
///      - network_id       (UUID)
///      - web3_unique_id   (0x-prefixed 20-byte hex)
contract MoneroNetworkRegistry {
    error NotOwner();
    error NotWriter();
    error InvalidHexAddressLike(string value);
    error InvalidNetworkName(string value);
    error EmptyString(string field);
    error AlreadyExists(bytes32 key);
    error NotFound(bytes32 key);

    event OwnerTransferred(address indexed previousOwner, address indexed newOwner);
    event WriterSet(address indexed writer, bool enabled);
    event ContractPaused(address indexed by);
    event ContractUnpaused(address indexed by);
    event ProfileUpserted(
        bytes32 indexed key,
        string indexed network,
        string chainIdHex,
        string networkId,
        string web3UniqueId,
        string daemonEndpoint,
        string daemonVersion,
        uint64 updatedAt
    );
    event ProfileRemoved(bytes32 indexed key, string indexed network, string networkId);

    struct Profile {
        string network;
        string chainIdHex;
        string networkId;
        string web3UniqueId;
        string daemonEndpoint;
        string daemonVersion;
        uint64 updatedAt;
        address updatedBy;
    }

    address public owner;
    bool public paused;

    mapping(address => bool) public writers;
    mapping(bytes32 => Profile) private profiles;
    mapping(bytes32 => bool) private exists;

    constructor(address initialOwner) {
        owner = initialOwner == address(0) ? msg.sender : initialOwner;
        writers[owner] = true;
        emit OwnerTransferred(address(0), owner);
        emit WriterSet(owner, true);
    }

    modifier onlyOwner() {
        if (msg.sender != owner) revert NotOwner();
        _;
    }

    modifier onlyWriter() {
        if (!writers[msg.sender]) revert NotWriter();
        _;
    }

    modifier whenNotPaused() {
        require(!paused, "paused");
        _;
    }

    function transferOwnership(address newOwner) external onlyOwner {
        require(newOwner != address(0), "owner=0");
        address oldOwner = owner;
        owner = newOwner;
        writers[newOwner] = true;
        emit OwnerTransferred(oldOwner, newOwner);
        emit WriterSet(newOwner, true);
    }

    function setWriter(address writer, bool enabled) external onlyOwner {
        writers[writer] = enabled;
        emit WriterSet(writer, enabled);
    }

    function pause() external onlyOwner {
        paused = true;
        emit ContractPaused(msg.sender);
    }

    function unpause() external onlyOwner {
        paused = false;
        emit ContractUnpaused(msg.sender);
    }

    function profileKey(
        string calldata network,
        string calldata networkId,
        string calldata web3UniqueId
    ) public pure returns (bytes32) {
        return keccak256(abi.encodePacked(network, "|", networkId, "|", web3UniqueId));
    }

    function upsertProfile(
        string calldata network,
        string calldata chainIdHex,
        string calldata networkId,
        string calldata web3UniqueId,
        string calldata daemonEndpoint,
        string calldata daemonVersion
    ) external onlyWriter whenNotPaused returns (bytes32 key) {
        _validateNetwork(network);
        _requireNonEmpty("chainIdHex", chainIdHex);
        _requireNonEmpty("networkId", networkId);
        _requireNonEmpty("daemonEndpoint", daemonEndpoint);
        _requireNonEmpty("daemonVersion", daemonVersion);
        _validateHexAddressLike(web3UniqueId);

        key = profileKey(network, networkId, web3UniqueId);
        bool existed = exists[key];

        profiles[key] = Profile({
            network: network,
            chainIdHex: chainIdHex,
            networkId: networkId,
            web3UniqueId: web3UniqueId,
            daemonEndpoint: daemonEndpoint,
            daemonVersion: daemonVersion,
            updatedAt: uint64(block.timestamp),
            updatedBy: msg.sender
        });
        exists[key] = true;

        emit ProfileUpserted(
            key,
            network,
            chainIdHex,
            networkId,
            web3UniqueId,
            daemonEndpoint,
            daemonVersion,
            uint64(block.timestamp)
        );

        // Return value informs caller if this was an insert vs update.
        if (!existed) {
            return key;
        }
    }

    function createProfile(
        string calldata network,
        string calldata chainIdHex,
        string calldata networkId,
        string calldata web3UniqueId,
        string calldata daemonEndpoint,
        string calldata daemonVersion
    ) external onlyWriter whenNotPaused returns (bytes32 key) {
        key = profileKey(network, networkId, web3UniqueId);
        if (exists[key]) revert AlreadyExists(key);
        return upsertProfile(network, chainIdHex, networkId, web3UniqueId, daemonEndpoint, daemonVersion);
    }

    function removeProfile(bytes32 key) external onlyWriter whenNotPaused {
        if (!exists[key]) revert NotFound(key);

        Profile memory p = profiles[key];
        delete profiles[key];
        delete exists[key];

        emit ProfileRemoved(key, p.network, p.networkId);
    }

    function getProfile(bytes32 key) external view returns (Profile memory) {
        if (!exists[key]) revert NotFound(key);
        return profiles[key];
    }

    function hasProfile(bytes32 key) external view returns (bool) {
        return exists[key];
    }

    function _requireNonEmpty(string memory field, string calldata value) private pure {
        if (bytes(value).length == 0) revert EmptyString(field);
    }

    function _validateNetwork(string calldata network) private pure {
        bytes32 h = keccak256(bytes(network));
        if (
            h != keccak256("mainnet") &&
            h != keccak256("testnet") &&
            h != keccak256("stagenet") &&
            h != keccak256("fakechain")
        ) {
            revert InvalidNetworkName(network);
        }
    }

    function _validateHexAddressLike(string calldata value) private pure {
        bytes calldata b = bytes(value);
        if (b.length != 42 || b[0] != "0" || (b[1] != "x" && b[1] != "X")) {
            revert InvalidHexAddressLike(value);
        }

        for (uint256 i = 2; i < b.length; i++) {
            bytes1 c = b[i];
            bool isNum = c >= "0" && c <= "9";
            bool isLower = c >= "a" && c <= "f";
            bool isUpper = c >= "A" && c <= "F";
            if (!(isNum || isLower || isUpper)) {
                revert InvalidHexAddressLike(value);
            }
        }
    }
}
