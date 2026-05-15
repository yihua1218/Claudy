#!/usr/bin/env python3
"""Codex-specific entrypoint for Claudy hook events."""
import os
import send_state


if __name__ == "__main__":
    send_state.run(os.environ.get("CLAUDY_CLIENT", "codex-vscode-remote"))
