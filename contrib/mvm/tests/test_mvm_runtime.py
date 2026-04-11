import unittest

from contrib.mvm.mvm_runtime import MVM, MVMError


class TestMVMRuntime(unittest.TestCase):
    def test_token_flow(self) -> None:
        program = [
            {"op": "CREATE_CONTRACT", "contract_id": "c1", "code_hash": "h1", "creator": "alice"},
            {
                "op": "CREATE_TOKEN",
                "contract_id": "c1",
                "symbol": "USDT",
                "owner": "issuer",
                "supply": 1000,
                "decimals": 6,
                "monero_tx": {
                    "txid": "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    "block_height": 100,
                },
            },
            {"op": "TRANSFER_TOKEN", "symbol": "USDT", "from": "issuer", "to": "alice", "amount": 250},
            {"op": "HALT"},
        ]
        state = MVM(program, context={"block_height": 100}).run()
        self.assertTrue(state.halted)
        self.assertEqual(state.tokens["USDT"]["balances"]["issuer"], 750)
        self.assertEqual(state.tokens["USDT"]["balances"]["alice"], 250)
        self.assertEqual(
            state.tokens["USDT"]["monero_tx"]["txid"],
            "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        )
        self.assertEqual(state.tokens["USDT"]["monero_tx"]["block_height"], 100)

    def test_token_creation_height_must_match_execution_height(self) -> None:
        program = [
            {"op": "CREATE_CONTRACT", "contract_id": "c1", "code_hash": "h1", "creator": "issuer"},
            {
                "op": "CREATE_TOKEN",
                "contract_id": "c1",
                "symbol": "USDT",
                "owner": "issuer",
                "supply": 1000,
                "decimals": 6,
                "monero_tx": {
                    "txid": "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    "block_height": 99,
                },
            }
        ]
        with self.assertRaisesRegex(MVMError, "deployment height must match current block height"):
            MVM(program, context={"block_height": 100}).run()

    def test_enforces_max_steps(self) -> None:
        # Infinite loop: jump back to self.
        program = [{"op": "JUMP", "target": 0}]
        with self.assertRaises(MVMError):
            MVM(program, max_steps=10).run()

    def test_token_requires_existing_contract(self) -> None:
        program = [
            {
                "op": "CREATE_TOKEN",
                "contract_id": "missing_contract",
                "symbol": "USDT",
                "owner": "issuer",
                "supply": 1000,
                "decimals": 6,
                "monero_tx": {
                    "txid": "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    "block_height": 100,
                },
            }
        ]
        with self.assertRaisesRegex(MVMError, "contract 'missing_contract' not found"):
            MVM(program, context={"block_height": 100}).run()

    def test_rejects_invalid_monero_txid(self) -> None:
        program = [
            {"op": "CREATE_CONTRACT", "contract_id": "c1", "code_hash": "h1", "creator": "issuer"},
            {
                "op": "CREATE_TOKEN",
                "contract_id": "c1",
                "symbol": "USDT",
                "owner": "issuer",
                "supply": 1000,
                "decimals": 6,
                "monero_tx": {"txid": "not-a-valid-txid", "block_height": 100},
            },
        ]
        with self.assertRaisesRegex(MVMError, "deployment txid must be 64 hex chars"):
            MVM(program, context={"block_height": 100}).run()

    def test_rejects_mint_when_supply_cap_exceeded(self) -> None:
        max_supply = 10**18
        program = [
            {"op": "CREATE_CONTRACT", "contract_id": "c1", "code_hash": "h1", "creator": "issuer"},
            {
                "op": "CREATE_TOKEN",
                "contract_id": "c1",
                "symbol": "USDT",
                "owner": "issuer",
                "supply": max_supply,
                "decimals": 6,
                "monero_tx": {
                    "txid": "0xaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
                    "block_height": 100,
                },
            },
            {"op": "MINT_TOKEN", "symbol": "USDT", "to": "issuer", "amount": 1},
        ]
        with self.assertRaisesRegex(MVMError, "mint would exceed max token supply"):
            MVM(program, context={"block_height": 100}).run()


if __name__ == "__main__":
    unittest.main()
