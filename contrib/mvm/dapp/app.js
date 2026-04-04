const evmAccountEl = document.getElementById('evm-account');
const evmChainEl = document.getElementById('evm-chain');
const moneroAccountEl = document.getElementById('monero-account');
const networkJsonEl = document.getElementById('network-json');
const payloadJsonEl = document.getElementById('payload-json');

const truncate = (s) => s && s.length > 18 ? `${s.slice(0, 10)}...${s.slice(-6)}` : s;

async function connectEvm() {
  if (!window.ethereum) {
    alert('No EVM wallet found (e.g. MetaMask).');
    return;
  }
  const [account] = await window.ethereum.request({ method: 'eth_requestAccounts' });
  const chainId = await window.ethereum.request({ method: 'eth_chainId' });
  evmAccountEl.textContent = truncate(account);
  evmChainEl.textContent = chainId;
}

async function connectMonero() {
  if (!window.monero) {
    alert('No Monero browser wallet provider detected.');
    return;
  }
  const address = await window.monero.connect();
  moneroAccountEl.textContent = truncate(address);
}

async function loadNetwork() {
  const base = document.getElementById('daemon-url').value.trim().replace(/\/$/, '');
  const res = await fetch(`${base}/get_web3_network`);
  const json = await res.json();
  networkJsonEl.textContent = JSON.stringify(json, null, 2);
}

function buildPayload() {
  const bytecode = document.getElementById('bytecode').value.trim().replace(/^0x/i, '');
  const payload = {
    action: 'create_contract',
    bytecode_hex: bytecode,
    created_at: new Date().toISOString(),
    wallet: {
      evm_account: evmAccountEl.textContent,
      monero_account: moneroAccountEl.textContent,
      chain_id: evmChainEl.textContent,
    },
  };
  payloadJsonEl.textContent = JSON.stringify(payload, null, 2);
}

document.getElementById('connect-evm').addEventListener('click', () => connectEvm().catch((e) => alert(e.message)));
document.getElementById('connect-monero').addEventListener('click', () => connectMonero().catch((e) => alert(e.message)));
document.getElementById('load-network').addEventListener('click', () => loadNetwork().catch((e) => alert(e.message)));
document.getElementById('build-payload').addEventListener('click', buildPayload);
