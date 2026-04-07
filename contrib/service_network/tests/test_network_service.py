import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3]))

from contrib.service_network.network_service import (
    AuthError,
    RateLimitError,
    ServiceNetwork,
    build_grant_requirements,
)


def make_network() -> ServiceNetwork:
    network = ServiceNetwork(salt="fixed-salt")
    network.auth.register_subscriber(
        subscriber_id="sub_1",
        raw_api_key="secret-key",
        tier="starter",
        salt="fixed-salt",
        allowed_ips=["10.0.0.1"],
    )
    return network


def test_send_message_and_pull() -> None:
    network = make_network()
    message_id = network.send_message(
        subscriber_id="sub_1",
        api_key="secret-key",
        source_ip="10.0.0.1",
        sender="alice",
        recipient="bob",
        payload=b"hello",
    )

    assert message_id
    inbox = network.router.pull("bob")
    assert len(inbox) == 1
    assert inbox[0].payload == b"hello"


def test_auth_rejects_wrong_ip() -> None:
    network = make_network()

    try:
        network.send_message(
            subscriber_id="sub_1",
            api_key="secret-key",
            source_ip="10.0.0.2",
            sender="alice",
            recipient="bob",
            payload=b"hello",
        )
    except AuthError:
        pass
    else:
        raise AssertionError("expected AuthError")


def test_rate_limit_exceeded() -> None:
    network = make_network()
    network._tier_limits["starter"] = (0.0, 1)

    network.send_message(
        subscriber_id="sub_1",
        api_key="secret-key",
        source_ip="10.0.0.1",
        sender="alice",
        recipient="bob",
        payload=b"one",
    )

    try:
        network.send_message(
            subscriber_id="sub_1",
            api_key="secret-key",
            source_ip="10.0.0.1",
            sender="alice",
            recipient="bob",
            payload=b"two",
        )
    except RateLimitError:
        pass
    else:
        raise AssertionError("expected RateLimitError")


def test_start_and_accept_call() -> None:
    network = make_network()
    call = network.start_call(
        subscriber_id="sub_1",
        api_key="secret-key",
        source_ip="10.0.0.1",
        caller="alice",
        callee="bob",
    )
    accepted = network.calls.signal_accept(call.session_id, "bob")
    assert accepted.state == "connected"


def test_build_grant_requirements() -> None:
    payload = build_grant_requirements("svc-chat", "dev-app", 12)
    assert payload["required_amount_xmr"] == 48
    assert payload["allowed_networks"] == ["ip", "onion"]
