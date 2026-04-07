"""Secure, fast core primitives for a communication service network.

This module is intentionally framework-agnostic so it can be embedded into an
HTTP/WebSocket service (FastAPI, aiohttp, etc.) without changing business logic.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hmac
import secrets
import threading
import time
from collections import defaultdict, deque
from typing import Deque, Dict, Iterable, Optional, Set, Tuple


class AuthError(Exception):
    """Authentication/authorization failure."""


class RateLimitError(Exception):
    """Rate limit exceeded."""


class NotFoundError(Exception):
    """Requested entity does not exist."""


@dataclass(frozen=True)
class Subscriber:
    subscriber_id: str
    api_key_hash: str
    tier: str
    allowed_ips: Tuple[str, ...] = ()


class ApiKeyStore:
    """Constant-time API key verification.

    Notes for security:
    - Keys are never stored in plaintext.
    - Comparison uses hmac.compare_digest to reduce timing side channels.
    """

    def __init__(self) -> None:
        self._by_id: Dict[str, Subscriber] = {}
        self._lock = threading.RLock()

    @staticmethod
    def hash_key(raw_key: str, *, salt: str) -> str:
        # Fast and deterministic for demo purposes; production should use
        # Argon2id or scrypt with per-key random salts.
        import hashlib

        return hashlib.sha256((salt + raw_key).encode("utf-8")).hexdigest()

    def register_subscriber(
        self,
        subscriber_id: str,
        raw_api_key: str,
        tier: str,
        *,
        salt: str,
        allowed_ips: Optional[Iterable[str]] = None,
    ) -> Subscriber:
        key_hash = self.hash_key(raw_api_key, salt=salt)
        sub = Subscriber(
            subscriber_id=subscriber_id,
            api_key_hash=key_hash,
            tier=tier,
            allowed_ips=tuple(allowed_ips or ()),
        )
        with self._lock:
            self._by_id[subscriber_id] = sub
        return sub

    def authenticate(
        self,
        subscriber_id: str,
        raw_api_key: str,
        *,
        salt: str,
        source_ip: Optional[str] = None,
    ) -> Subscriber:
        with self._lock:
            sub = self._by_id.get(subscriber_id)

        if sub is None:
            raise AuthError("invalid subscriber")

        candidate = self.hash_key(raw_api_key, salt=salt)
        if not hmac.compare_digest(candidate, sub.api_key_hash):
            raise AuthError("invalid credentials")

        if sub.allowed_ips and source_ip not in sub.allowed_ips:
            raise AuthError("ip not allowed")

        return sub


class TokenBucketLimiter:
    """Thread-safe token bucket implementation for high-throughput APIs."""

    def __init__(self) -> None:
        self._state: Dict[str, Tuple[float, float]] = {}
        self._lock = threading.RLock()

    def allow(self, key: str, *, rate: float, burst: int, now: Optional[float] = None) -> bool:
        t = time.monotonic() if now is None else now
        with self._lock:
            tokens, last = self._state.get(key, (float(burst), t))
            elapsed = max(0.0, t - last)
            tokens = min(float(burst), tokens + elapsed * rate)
            if tokens < 1.0:
                self._state[key] = (tokens, t)
                return False

            tokens -= 1.0
            self._state[key] = (tokens, t)
            return True


@dataclass
class Message:
    message_id: str
    sender: str
    recipient: str
    payload: bytes
    created_at_ms: int = field(default_factory=lambda: int(time.time() * 1000))


class MessageRouter:
    """Low-latency in-memory message routing with bounded queues."""

    def __init__(self, *, queue_limit: int = 1000, max_payload_bytes: int = 65536) -> None:
        self._inboxes: Dict[str, Deque[Message]] = defaultdict(deque)
        self._queue_limit = queue_limit
        self._max_payload_bytes = max_payload_bytes
        self._lock = threading.RLock()

    def route(self, sender: str, recipient: str, payload: bytes) -> str:
        if not payload:
            raise ValueError("payload cannot be empty")
        if len(payload) > self._max_payload_bytes:
            raise ValueError("payload too large")

        msg_id = secrets.token_urlsafe(18)
        msg = Message(message_id=msg_id, sender=sender, recipient=recipient, payload=payload)

        with self._lock:
            inbox = self._inboxes[recipient]
            if len(inbox) >= self._queue_limit:
                # Drop oldest for bounded memory and consistent latency.
                inbox.popleft()
            inbox.append(msg)
        return msg_id

    def pull(self, recipient: str, *, limit: int = 100) -> list[Message]:
        if limit <= 0:
            return []
        with self._lock:
            inbox = self._inboxes[recipient]
            out: list[Message] = []
            for _ in range(min(limit, len(inbox))):
                out.append(inbox.popleft())
            return out


@dataclass
class CallSession:
    session_id: str
    caller: str
    callee: str
    state: str
    participants: Set[str]
    created_at_ms: int = field(default_factory=lambda: int(time.time() * 1000))


class CallSignalingService:
    """Manages secure call setup lifecycle for 1:1 or group calls."""

    def __init__(self) -> None:
        self._sessions: Dict[str, CallSession] = {}
        self._lock = threading.RLock()

    def start_call(self, caller: str, callee: str) -> CallSession:
        sid = secrets.token_urlsafe(24)
        session = CallSession(
            session_id=sid,
            caller=caller,
            callee=callee,
            state="ringing",
            participants={caller, callee},
        )
        with self._lock:
            self._sessions[sid] = session
        return session

    def add_participant(self, session_id: str, user_id: str) -> None:
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                raise NotFoundError("session not found")
            session.participants.add(user_id)

    def signal_accept(self, session_id: str, user_id: str) -> CallSession:
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                raise NotFoundError("session not found")
            if user_id not in session.participants:
                raise AuthError("not in call")
            session.state = "connected"
            return session

    def end_call(self, session_id: str, user_id: str) -> None:
        with self._lock:
            session = self._sessions.get(session_id)
            if session is None:
                raise NotFoundError("session not found")
            if user_id not in session.participants:
                raise AuthError("not in call")
            session.state = "ended"
            del self._sessions[session_id]


class ServiceNetwork:
    """Facade combining auth, rate limiting, messaging and call signaling."""

    def __init__(self, *, salt: str) -> None:
        self._salt = salt
        self.auth = ApiKeyStore()
        self.limiter = TokenBucketLimiter()
        self.router = MessageRouter()
        self.calls = CallSignalingService()
        # Requests/second and burst by subscription tier.
        self._tier_limits = {
            "starter": (20.0, 40),
            "growth": (100.0, 200),
            "enterprise": (500.0, 1000),
        }

    def _enforce_rate_limit(self, sub: Subscriber, endpoint: str) -> None:
        rate, burst = self._tier_limits.get(sub.tier, (10.0, 20))
        key = f"{sub.subscriber_id}:{endpoint}"
        if not self.limiter.allow(key, rate=rate, burst=burst):
            raise RateLimitError("too many requests")

    def send_message(
        self,
        *,
        subscriber_id: str,
        api_key: str,
        source_ip: str,
        sender: str,
        recipient: str,
        payload: bytes,
    ) -> str:
        sub = self.auth.authenticate(
            subscriber_id,
            api_key,
            salt=self._salt,
            source_ip=source_ip,
        )
        self._enforce_rate_limit(sub, "send_message")
        return self.router.route(sender=sender, recipient=recipient, payload=payload)

    def start_call(
        self,
        *,
        subscriber_id: str,
        api_key: str,
        source_ip: str,
        caller: str,
        callee: str,
    ) -> CallSession:
        sub = self.auth.authenticate(
            subscriber_id,
            api_key,
            salt=self._salt,
            source_ip=source_ip,
        )
        self._enforce_rate_limit(sub, "start_call")
        return self.calls.start_call(caller=caller, callee=callee)


def build_grant_requirements(service_id: str, subscriber: str, months: int) -> dict:
    plan_amounts = {1: 8, 2: 10, 12: 48}
    if months not in plan_amounts:
        raise ValueError("months must be one of 1, 2, 12")

    return {
        "service_id": service_id,
        "subscriber": subscriber,
        "required_amount_xmr": plan_amounts[months],
        "allowed_networks": ["ip", "onion"],
        "p2p_commands": [
            "COMMAND_NOTIFY_SERVICE_SUBSCRIPTION",
            "COMMAND_NOTIFY_SERVICE_ACCESS_GRANT",
        ],
    }
