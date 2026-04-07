#!/usr/bin/env python3
"""Simple interactive assistant that explains this Monero repository.

The assistant is intentionally lightweight and dependency-free so contributors can
run it in any standard Python 3 environment.
"""

from __future__ import annotations

import argparse
import re
from dataclasses import dataclass


@dataclass(frozen=True)
class Topic:
    name: str
    summary: str


PROJECT_TOPICS = [
    Topic(
        name="overview",
        summary=(
            "This repository is the core implementation of Monero: a private, "
            "secure, and decentralized digital currency. It includes the daemon, "
            "CLI wallet, RPC services, and supporting tooling."
        ),
    ),
    Topic(
        name="privacy",
        summary=(
            "Monero protects transaction privacy by default using ring signatures, "
            "stealth addresses, and confidential transactions."
        ),
    ),
    Topic(
        name="security",
        summary=(
            "Security is maintained through cryptographic proofs, open-source "
            "review, reproducible builds, and an active testing and review culture."
        ),
    ),
    Topic(
        name="decentralization",
        summary=(
            "The network is peer-to-peer and designed for commodity hardware. "
            "Anyone can run a node and verify the chain independently."
        ),
    ),
    Topic(
        name="contributing",
        summary=(
            "Contributions are welcome via pull requests. Read docs/CONTRIBUTING.md "
            "and coordinate changes with the developer community."
        ),
    ),
]

GREETINGS = (
    "hi",
    "hello",
    "hey",
    "gm",
    "good morning",
    "good afternoon",
    "good evening",
)


def find_topic(text: str) -> Topic | None:
    lowered = text.lower()
    for topic in PROJECT_TOPICS:
        if re.search(rf"\b{re.escape(topic.name)}\b", lowered):
            return topic
    if "what is this" in lowered or "what is monero" in lowered:
        return PROJECT_TOPICS[0]
    return None


def respond(message: str) -> str:
    lowered = message.strip().lower()

    if not lowered:
        return "Say something and I'll do my best to help."

    if lowered in {"quit", "exit", "bye"}:
        return "Goodbye!"

    if any(lowered.startswith(greeting) for greeting in GREETINGS):
        return (
            "Hey! I'm the Monero project assistant. Ask me about overview, privacy, "
            "security, decentralization, or contributing."
        )

    if "help" in lowered:
        topics = ", ".join(topic.name for topic in PROJECT_TOPICS)
        return f"You can ask about: {topics}. Type 'exit' to leave."

    topic = find_topic(lowered)
    if topic:
        return topic.summary

    if "talk" in lowered or "interact" in lowered or "chat" in lowered:
        return (
            "Absolutely — we can chat. Ask a question about this repo and I'll "
            "explain it in plain language."
        )

    return (
        "I don't have a specific answer for that yet, but I can explain the project "
        "overview, privacy model, security approach, decentralization, and how to "
        "contribute."
    )


def run_interactive() -> None:
    print("Monero Assistant: Hi! Ask me what this project is about. (type 'exit' to quit)")
    while True:
        try:
            user_message = input("You: ")
        except EOFError:
            print("\nMonero Assistant: Goodbye!")
            return

        answer = respond(user_message)
        print(f"Monero Assistant: {answer}")

        if user_message.strip().lower() in {"quit", "exit", "bye"}:
            return


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Interactive Monero assistant for project explanations."
    )
    parser.add_argument(
        "--message",
        help="Single message mode. If omitted, interactive mode starts.",
    )
    args = parser.parse_args()

    if args.message:
        print(respond(args.message))
        return

    run_interactive()


if __name__ == "__main__":
    main()
