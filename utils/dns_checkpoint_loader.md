# dns_checkpoint_loader.py

This utility is a simple `dns.txt` loader and **does not require DNSSEC**.

It reads checkpoint rows from either:
- an HTTP/HTTPS URL (for example an nginx-hosted file), or
- a local file path.

For daemon-side custom checkpoint loading, set:

```bash
MONERO_DNS_CHECKPOINTS_SOURCE=checkpoints.moneropulse.org
```

The loader accepts:
- full URL (`http://8.9.9.9/dns.txt`, `https://checkpoints.moneropulse.org/dns.txt`)
- bare host/IP (`checkpoints.moneropulse.org`, `8.9.9.9`) which is interpreted as `http://<host>/dns.txt`
- local file path.

Each row should be:

```text
block_height,block_hash
```

Accepted separators are comma `,`, colon `:`, or whitespace.

## Example nginx setup

Host a `dns.txt` file like:

```text
100,aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
200,bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb
```

Then expose it through nginx, for example:

```nginx
location /dns.txt {
    alias /var/www/checkpoints/dns.txt;
    default_type text/plain;
}
```

## Usage examples

Query a single height:

```bash
python3 utils/dns_checkpoint_loader.py --source http://YOUR_IP/dns.txt --height 200
```

Download/save the file locally too:

```bash
python3 utils/dns_checkpoint_loader.py --source http://YOUR_IP/dns.txt --download-out /tmp/dns.txt
```

Print all rows as JSON:

```bash
python3 utils/dns_checkpoint_loader.py --source http://YOUR_IP/dns.txt --json
```
