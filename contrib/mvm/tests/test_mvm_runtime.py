import unittest

from contrib.mvm.mvm_runtime import MVM, MVMError


class TestMVMRuntime(unittest.TestCase):
    def test_token_flow(self) -> None:
        program = [
            {"op": "CREATE_CONTRACT", "contract_id": "c1", "code_hash": "h1", "creator": "alice"},
            {"op": "CREATE_TOKEN", "contract_id": "c1", "symbol": "USDT", "owner": "issuer", "supply": 1000, "decimals": 6},
            {"op": "TRANSFER_TOKEN", "symbol": "USDT", "from": "issuer", "to": "alice", "amount": 250},
            {"op": "HALT"},
        ]
        state = MVM(program, context={"block_height": 100}).run()
        self.assertTrue(state.halted)
        self.assertEqual(state.tokens["USDT"]["balances"]["issuer"], 750)
        self.assertEqual(state.tokens["USDT"]["balances"]["alice"], 250)

    def test_enforces_max_steps(self) -> None:
        # Infinite loop: jump back to self.
        program = [{"op": "JUMP", "target": 0}]
        with self.assertRaises(MVMError):
            MVM(program, max_steps=10).run()


if __name__ == "__main__":
    unittest.main()
