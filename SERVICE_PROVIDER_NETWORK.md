# Service Provider Network Blueprint

## One-line value proposition
A subscription-based communication network that lets third-party apps use our infrastructure to route messages and establish voice/video calls through a unified API.

## What this service provides
- **Messaging as a service**: apps send/receive text, media, and status events through our network.
- **Call setup as a service**: apps use our signaling and session management to establish 1:1 or group calls.
- **Interoperable routing**: we handle connection discovery, routing, retries, and quality optimization.
- **Developer onboarding**: partners join the network, subscribe to a plan, and integrate with SDKs/APIs.

## Product model
1. A company/developer **joins the network**.
2. They **subscribe** to a service tier (usage, SLA, features).
3. They integrate our **API/SDK** for messaging and calling.
4. Our network handles **transport, routing, and connection establishment**.
5. The partner app focuses on UX while we provide reliability, scale, and observability.
6. Providers can advertise service endpoints on **IP peers or onion peers** in the P2P network.

## Core platform capabilities
- Identity and authentication (users, devices, apps)
- Presence and reachability
- Message queueing and delivery receipts
- Call signaling (offer/answer/ICE exchange) and TURN/STUN support
- End-to-end encryption options and key management controls
- QoS routing and failover across regions
- Billing, metering, and usage analytics
- Compliance controls (retention policy, audit logs, regional data handling)

## Suggested API surface
- `POST /v1/messages/send`
- `POST /v1/messages/webhook`
- `POST /v1/calls/start`
- `POST /v1/calls/signal`
- `POST /v1/calls/end`
- `GET /v1/network/presence/{userId}`
- `GET /v1/usage/metrics`

## Commercial packaging
- **Starter**: low-volume messaging and basic call setup
- **Growth**: higher throughput, advanced analytics, multi-region routing
- **Enterprise**: custom SLA, dedicated support, private connectivity options

## Subscription pricing example
- **1 month** subscription: **8 XMR**
- **2 months** subscription: **10 XMR**
- **12 months** subscription: **48 XMR**

These terms are reflected in the core node code as service-network subscription quote checks so only expected pricing plans are relayed.

## Provider registration in `monerod`
Run:

`./monerod --be_a_service_provider <address>`

The daemon validates the payment address and announces it over the service-network P2P layer so app developers can discover available providers and pay the required subscription amount.

Runtime daemon commands:
- `service_provider_list`
- `check_grant <service_id> <subscriber> <months>`

## Access after payment confirmation
When a subscription payment is received and confirmed, the network generates a **service access grant** for the app developer.
This grant includes:
- an access token,
- allowed networks (`ip` and `onion`),
- validity window tied to the paid subscription period.

The grant is propagated through the P2P service-network command flow so apps can use it to connect over both secured onion routes and regular IP routes.

## Amount consensus guard / monitoring
Service nodes include an always-on subscription amount guard that continuously verifies the hardcoded pricing checksum.
If amounts are modified from consensus values, the node stops accepting service subscriptions and emits a fatal monitor log to prevent silent price tampering.
The masternode guardian check is also surfaced through `service_provider_list`.

## Refined pitch text
We provide a communications network-as-a-service. Any product can subscribe to our network and use our APIs to route messages or establish calls. If you're building a messaging platform, you connect to our network once and we provide the infrastructure needed for reliable message delivery, real-time signaling, and end-to-end connection setup.
