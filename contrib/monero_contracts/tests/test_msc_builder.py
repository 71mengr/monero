import unittest

from contrib.monero_contracts.msc_builder import build_contract_package


class TestMSCBuilder(unittest.TestCase):
    def _source(self):
        return {
            "name": "Escrow A",
            "contract_type": "escrow",
            "network": "mainnet",
            "parties": [{"alias": "buyer"}, {"alias": "seller"}],
            "rules": {"unlock": "oracle == 1"},
            "actions": [{"type": "release_multisig"}],
        }

    def test_hash_stable_across_created_at(self) -> None:
        src = self._source()
        p1 = build_contract_package(src, rpc_meta=None, created_at="2026-01-01T00:00:00+00:00")
        p2 = build_contract_package(src, rpc_meta=None, created_at="2026-01-02T00:00:00+00:00")
        self.assertNotEqual(p1["created_at"], p2["created_at"])
        self.assertEqual(p1["package_hash"], p2["package_hash"])
        self.assertEqual(p1["contract_id"], p2["contract_id"])

    def test_duplicate_party_alias_rejected(self) -> None:
        src = self._source()
        src["parties"] = [{"alias": "buyer"}, {"alias": "buyer"}]
        with self.assertRaises(ValueError):
            build_contract_package(src, rpc_meta=None)


if __name__ == "__main__":
    unittest.main()
