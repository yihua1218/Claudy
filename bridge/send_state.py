#!/usr/bin/env python3
"""
Claudy bridge: maps agent hook JSON on stdin to an ESP32 state update.

Supported today:
  - Claude Code hooks via bridge/send-state.sh
  - Codex hooks via bridge/send-codex-state.sh

Configure with environment variables:
  CLAUDY_URL         full /state URL (default: http://claudy.local/state)
  CLAUDY_TOKEN       optional shared secret matching firmware AUTH_TOKEN
  CLAUDY_MAX_TOKENS  context budget override for the progress bar
  CLAUDY_CLIENT      source label sent to firmware/mock displays
"""
import json
import os
import re
import socket
import subprocess
import sys
import time
import urllib.request
from urllib.parse import urlparse, urlunparse

CLAUDY_URL = os.environ.get("CLAUDY_URL", "http://claudy.local/state")
AUTH_TOKEN = os.environ.get("CLAUDY_TOKEN", "")
MAX_TOKENS_OVERRIDE = int(os.environ.get("CLAUDY_MAX_TOKENS", "0"))
CLIENT = os.environ.get("CLAUDY_CLIENT", "claude-code")
_CACHE_BASE = os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache"))
IP_CACHE = os.environ.get("CLAUDY_IP_CACHE", os.path.join(_CACHE_BASE, "claudy", "ip_cache"))
LEGACY_IP_CACHE = os.path.expanduser("~/.claude/claudy_ip_cache")
IP_CACHE_TTL = 60


def brief(s, n=58):
    if s is None:
        return ""
    s = str(s).replace("\n", " ").strip()
    return (s[: n - 1] + "...") if len(s) > n else s


def _basename(value):
    if not value:
        return ""
    return os.path.basename(str(value).replace("\\", "/"))


def _swap_host(url, ip):
    p = urlparse(url)
    netloc = f"{ip}:{p.port}" if p.port else ip
    return urlunparse(p._replace(netloc=netloc))


def _resolve_mdns(hostname, timeout=2.0):
    """Resolve a .local hostname using normal DNS, Linux tools, then macOS dns-sd."""
    try:
        return socket.gethostbyname(hostname)
    except Exception:
        pass

    ipv4 = re.compile(r"\b(\d{1,3}(?:\.\d{1,3}){3})\b")
    for cmd in (["avahi-resolve-host-name", "-4", hostname],
                ["getent", "ahostsv4", hostname]):
        try:
            proc = subprocess.run(
                cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
                text=True, timeout=timeout
            )
            m = ipv4.search(proc.stdout or "")
            if m:
                return m.group(1)
        except Exception:
            pass

    try:
        proc = subprocess.Popen(
            ["dns-sd", "-G", "v4", hostname],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True
        )
    except Exception:
        return None

    deadline = time.time() + timeout
    try:
        while time.time() < deadline:
            line = proc.stdout.readline()
            if not line:
                if proc.poll() is not None:
                    break
                continue
            if " Add " in line:
                m = ipv4.search(line)
                if m:
                    return m.group(1)
    finally:
        try:
            proc.terminate()
        except Exception:
            pass
    return None


def _resolve_url_with_cache(url):
    p = urlparse(url)
    host = p.hostname or ""
    if not host.endswith(".local"):
        return url

    for cache_path in (IP_CACHE, LEGACY_IP_CACHE):
        try:
            if os.path.exists(cache_path) and time.time() - os.path.getmtime(cache_path) < IP_CACHE_TTL:
                ip = open(cache_path).read().strip()
                if ip:
                    return _swap_host(url, ip)
        except Exception:
            pass

    ip = _resolve_mdns(host)
    if not ip:
        return url

    for cache_path in (IP_CACHE, LEGACY_IP_CACHE):
        try:
            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
            with open(cache_path, "w") as f:
                f.write(ip)
            break
        except Exception:
            continue
    return _swap_host(url, ip)


def _invalidate_ip_cache():
    for cache_path in (IP_CACHE, LEGACY_IP_CACHE):
        try:
            os.remove(cache_path)
        except Exception:
            pass


def post(payload):
    url = _resolve_url_with_cache(CLAUDY_URL)
    data = json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method="POST")
    req.add_header("Content-Type", "application/json")
    if AUTH_TOKEN:
        req.add_header("X-Claudy-Token", AUTH_TOKEN)
    try:
        urllib.request.urlopen(req, timeout=2.0).read()
    except Exception:
        _invalidate_ip_cache()


def _context_window_for_model(model, observed_used=0):
    if observed_used > 200000:
        return 1000000
    if model and ("[1m]" in model.lower() or "1m" in model.lower()):
        return 1000000
    return 200000


def tokens_from_transcript(path):
    """Best-effort token extraction from JSONL transcripts."""
    if not path or not os.path.exists(path):
        return (None, None)
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            if size > 65536:
                f.seek(size - 65536)
                f.readline()
            tail = f.read().decode("utf-8", errors="ignore").splitlines()
        for line in reversed(tail):
            try:
                obj = json.loads(line)
            except Exception:
                continue
            msg = obj.get("message") or {}
            usage = msg.get("usage") or obj.get("usage") or {}
            if not usage:
                continue
            used = (
                usage.get("input_tokens", 0)
                + usage.get("cache_read_input_tokens", 0)
                + usage.get("cache_creation_input_tokens", 0)
            )
            if used <= 0:
                continue
            model = msg.get("model") or obj.get("model") or ""
            return (used, _context_window_for_model(model, used))
    except Exception:
        return (None, None)
    return (None, None)


def _display_tool(tool, inp=None):
    inp = inp or {}
    if tool == "apply_patch":
        patch = str(inp.get("patch") or inp.get("command") or "")
        return "Write" if "*** Add File:" in patch else "Edit"
    if tool.startswith("mcp__"):
        return "Task"
    return tool


def _tool_message(tool, inp):
    if tool == "Bash":
        return brief(inp.get("command", ""))
    if tool == "apply_patch":
        return brief(inp.get("patch") or inp.get("command") or "apply patch")
    if tool == "Read":
        return brief(_basename(inp.get("file_path") or inp.get("path")))
    if tool in ("Edit", "MultiEdit", "Write"):
        return brief(_basename(inp.get("file_path") or inp.get("path")))
    if tool in ("Grep", "Glob"):
        return brief(inp.get("pattern") or inp.get("query", ""))
    if tool in ("WebFetch", "WebSearch"):
        return brief(inp.get("url") or inp.get("query", ""))
    if tool in ("Task", "Agent"):
        return brief(inp.get("description") or inp.get("prompt", ""))
    if tool.startswith("mcp__"):
        return brief(tool.split("__")[-1] or tool)
    return brief(tool)


def _tool_response_failed(ev):
    resp = ev.get("tool_response")
    if isinstance(resp, dict):
        if resp.get("success") is False or resp.get("ok") is False:
            return True
        code = resp.get("exit_code", resp.get("returncode"))
        if isinstance(code, int) and code != 0:
            return True
        status = str(resp.get("status", "")).lower()
        return status in ("error", "failed", "failure")
    if isinstance(resp, str):
        text = resp.lower()
        return "error" in text or "failed" in text
    return False


def map_event(ev):
    """Return (state, display_tool, message), or (None, None, None)."""
    name = ev.get("hook_event_name", "") or ev.get("agent_action_name", "")
    tool = ev.get("tool_name", "")
    inp = ev.get("tool_input") or {}

    if name == "SessionStart":
        return "idle", "", brief(ev.get("source") or "Session started")
    if name == "UserPromptSubmit":
        return "thinking", "", brief(ev.get("prompt", ""))
    if name == "PreToolUse":
        return "working", _display_tool(tool, inp), _tool_message(tool, inp)
    if name == "PostToolUse":
        display_tool = _display_tool(tool, inp)
        if _tool_response_failed(ev):
            return "error", display_tool, brief(f"{display_tool} failed")
        return "thinking", display_tool, brief(display_tool)
    if name == "PostToolUseFailure":
        display_tool = _display_tool(tool, inp)
        return "error", display_tool, brief(f"{display_tool} failed")
    if name in ("Notification", "PermissionRequest", "Elicitation"):
        display_tool = _display_tool(tool, inp)
        msg = ev.get("message") or ev.get("reason") or f"Permission: {display_tool or 'tool'}"
        return "waiting", display_tool, brief(msg)
    if name in ("Stop", "TaskCompleted"):
        return "done", "", "Done"
    if name in ("StopFailure", "PermissionDenied"):
        return "error", _display_tool(tool, inp), brief(name)
    if name == "SessionEnd":
        return "idle", "", "Idle"
    return None, None, None


def build_payload(ev, client=CLIENT):
    state, tool, msg = map_event(ev)
    if state is None:
        return None

    payload = {
        "state": state,
        "tool": tool,
        "message": msg,
        "event": ev.get("hook_event_name", "") or ev.get("agent_action_name", ""),
        "client": client,
    }

    model = ev.get("model") or ev.get("model_name")
    if model:
        payload["model"] = brief(model, 40)

    used, window = tokens_from_transcript(ev.get("transcript_path"))
    if used is not None:
        max_tokens = MAX_TOKENS_OVERRIDE if MAX_TOKENS_OVERRIDE > 0 else window
        payload["tokens"] = {"used": used, "max": max_tokens}

    return payload


def run(default_client=CLIENT):
    try:
        ev = json.load(sys.stdin)
    except Exception:
        return

    client = os.environ.get("CLAUDY_CLIENT", default_client)
    payload = build_payload(ev, client)
    if payload is None:
        return

    post(payload)
    if os.environ.get("CLAUDY_DEBUG_PAYLOAD"):
        print(json.dumps(payload, ensure_ascii=False), file=sys.stderr)

    if payload["state"] == "done":
        time.sleep(3)
        idle = {"state": "idle", "message": "Idle", "client": client}
        post(idle)
        if os.environ.get("CLAUDY_DEBUG_PAYLOAD"):
            print(json.dumps(idle, ensure_ascii=False), file=sys.stderr)


def main():
    run()


if __name__ == "__main__":
    main()
