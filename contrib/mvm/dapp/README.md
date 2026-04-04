# MVM dApp (Wallet Connect Demo)

A lightweight front-end dApp for:
- connecting an EVM wallet (via `window.ethereum`),
- connecting a Monero browser wallet provider (via `window.monero` if available),
- querying daemon `/get_web3_network`,
- building a draft MVM contract payload from bytecode.

## Run

```bash
cd contrib/mvm/dapp
python3 -m http.server 8787
```

Open:
- `http://127.0.0.1:8787`

## Notes

- EVM wallet requires an injected provider (e.g. MetaMask).
- Monero connect relies on `window.monero` provider support from the user's wallet extension.
