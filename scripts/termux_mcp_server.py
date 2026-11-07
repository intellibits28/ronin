#!/usr/bin/env python3
"""
Ronin Kernel - Hardened Termux MCP Server Implementation.
Exposes safe Termux tools via JSON-RPC 2.0 with HMAC-SHA256 signature verification,
nonce-based replay protection, and capability-isolated tool dispatch.
"""

import http.server
import json
import subprocess
import os
import hmac
import hashlib
import time
from collections import OrderedDict

PORT = 8765
SHARED_SECRET = os.environ.get("RONIN_MCP_SECRET", "").strip()

# Bounded LRU cache for replay prevention: stores {nonce: timestamp_sec}
SEEN_NONCES = OrderedDict()
MAX_SEEN_NONCES = 1000
REPLAY_WINDOW_SEC = 30

def check_and_record_nonce(nonce: str, now: int) -> bool:
    """Verifies that the nonce has not been seen before within the replay window."""
    if not nonce:
        return True
    
    # Purge expired nonces
    while SEEN_NONCES:
        old_nonce, old_time = next(iter(SEEN_NONCES.items()))
        if now - old_time > REPLAY_WINDOW_SEC * 2:
            SEEN_NONCES.pop(old_nonce)
        else:
            break

    if nonce in SEEN_NONCES:
        return False # Replay attack detected

    SEEN_NONCES[nonce] = now
    if len(SEEN_NONCES) > MAX_SEEN_NONCES:
        SEEN_NONCES.popitem(last=False)
    return True

class McpRequestHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        print(f"[MCP] {self.address_string()} - {format % args}")

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.end_headers()
        secret_status = "Configured (Protected)" if SHARED_SECRET else "Not Set (Open/Development)"
        msg = f"🚀 Ronin Termux MCP Server is RUNNING on port {PORT}\nStatus: OK\nShared Secret: {secret_status}\n\nConnect from Ronin App using /mcp status"
        self.wfile.write(msg.encode("utf-8"))

    def do_POST(self):
        content_len = int(self.headers.get("Content-Length", 0))
        raw_body = self.rfile.read(content_len).decode("utf-8")

        # Security Verification (HMAC-SHA256, Replay Window & Nonce Cache)
        timestamp_str = self.headers.get("X-Ronin-Timestamp", "")
        nonce = self.headers.get("X-Ronin-Nonce", "").strip()
        signature = self.headers.get("X-Ronin-Signature", "").strip()

        if SHARED_SECRET:
            try:
                req_time = int(timestamp_str) if timestamp_str else 0
                now = int(time.time())
                if abs(now - req_time) > REPLAY_WINDOW_SEC:
                    self.send_error(401, f"Timestamp replay window expired (clock drift: {abs(now - req_time)}s)")
                    return

                if nonce and not check_and_record_nonce(nonce, now):
                    self.send_error(401, "Replay attack detected: duplicate request nonce")
                    return

                # Support both nonce-authenticated and legacy payload formats
                expected_sig_with_nonce = hmac.new(
                    SHARED_SECRET.encode("utf-8"),
                    f"{req_time}.{nonce}.{raw_body}".encode("utf-8"),
                    hashlib.sha256
                ).hexdigest()
                expected_sig_legacy = hmac.new(
                    SHARED_SECRET.encode("utf-8"),
                    f"{req_time}.{raw_body}".encode("utf-8"),
                    hashlib.sha256
                ).hexdigest()

                valid = (
                    hmac.compare_digest(expected_sig_with_nonce.lower(), signature.lower()) or
                    hmac.compare_digest(expected_sig_legacy.lower(), signature.lower())
                )
                if not valid:
                    self.send_error(401, "Invalid HMAC signature")
                    return
            except Exception as e:
                self.send_error(401, f"Auth verification error: {str(e)}")
                return

        # Parse JSON-RPC 2.0 Request
        try:
            rpc_req = json.loads(raw_body)
            rpc_id = rpc_req.get("id", 1)
            method = rpc_req.get("method", "")
            params = rpc_req.get("params", {})
        except Exception:
            self.send_error(400, "Invalid JSON-RPC request format")
            return

        res = {"jsonrpc": "2.0", "id": rpc_id}

        # 1. Initialize Handshake
        if method == "initialize":
            res["result"] = {
                "protocolVersion": "2024-11-05",
                "serverInfo": {"name": "termux-mcp", "version": "1.1"},
                "capabilities": {"tools": {}}
            }

        # 2. List Available Tools (Strict Capability Execution Policy)
        elif method == "tools/list":
            res["result"] = {
                "tools": [
                    {
                        "name": "get_device_info",
                        "description": "Show Termux Linux kernel, architecture and system uptime",
                        "inputSchema": {"type": "object", "properties": {}}
                    },
                    {
                        "name": "list_files",
                        "description": "List files in Termux home or specified directory",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "path": {"type": "string", "description": "Directory path within user home"}
                            }
                        }
                    },
                    {
                        "name": "read_file",
                        "description": "Read text file contents safely within Termux home (max 128KB)",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "path": {"type": "string", "description": "File path within user home"}
                            },
                            "required": ["path"]
                        }
                    },
                    {
                        "name": "git_status",
                        "description": "Show git repository status in Termux home or project path",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "path": {"type": "string", "description": "Repository path"}
                            }
                        }
                    },
                    {
                        "name": "git_diff",
                        "description": "Show git repository diff in Termux home or project path",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "path": {"type": "string", "description": "Repository path"}
                            }
                        }
                    }
                ]
            }

        # 3. Call Tool (Capability-isolated dispatch, no unconstrained shell execution)
        elif method == "tools/call":
            tool_name = params.get("name")
            args = params.get("arguments", {})

            if tool_name == "get_device_info":
                uname = subprocess.run(["uname", "-a"], capture_output=True, text=True, timeout=5).stdout.strip()
                uptime = subprocess.run(["uptime"], capture_output=True, text=True, timeout=5).stdout.strip()
                whoami = subprocess.run(["whoami"], capture_output=True, text=True, timeout=5).stdout.strip()
                info_text = f"User: {whoami}\nSystem: {uname}\nUptime: {uptime}"
                res["result"] = {"content": [{"type": "text", "text": info_text}], "isError": False}

            elif tool_name == "list_files":
                raw_path = args.get("path", "")
                base_dir = os.path.realpath(os.path.expanduser("~"))
                target_dir = os.path.realpath(os.path.expanduser(raw_path)) if raw_path else base_dir

                # Enforce directory confinement to prevent path traversal (CWE-22)
                if not target_dir.startswith(base_dir):
                    res["result"] = {
                        "content": [{"type": "text", "text": f"Access denied: path '{raw_path}' is outside home directory"}],
                        "isError": True
                    }
                else:
                    try:
                        entries = sorted(os.listdir(target_dir))
                        res["result"] = {"content": [{"type": "text", "text": "\n".join(entries[:50])}], "isError": False}
                    except Exception as e:
                        res["result"] = {"content": [{"type": "text", "text": f"Error: {str(e)}"}], "isError": True}

            elif tool_name == "read_file":
                raw_path = args.get("path", "")
                if not raw_path:
                    res["result"] = {"content": [{"type": "text", "text": "Error: 'path' argument is required"}], "isError": True}
                else:
                    base_dir = os.path.realpath(os.path.expanduser("~"))
                    target_file = os.path.realpath(os.path.expanduser(raw_path))
                    if not target_file.startswith(base_dir):
                        res["result"] = {
                            "content": [{"type": "text", "text": f"Access denied: path '{raw_path}' is outside home directory"}],
                            "isError": True
                        }
                    elif not os.path.isfile(target_file):
                        res["result"] = {"content": [{"type": "text", "text": f"File not found: {raw_path}"}], "isError": True}
                    else:
                        try:
                            # 128 KB read limit
                            with open(target_file, "r", encoding="utf-8", errors="replace") as f:
                                content = f.read(128 * 1024)
                            res["result"] = {"content": [{"type": "text", "text": content}], "isError": False}
                        except Exception as e:
                            res["result"] = {"content": [{"type": "text", "text": f"Error reading file: {str(e)}"}], "isError": True}

            elif tool_name in ("git_status", "git_diff"):
                raw_path = args.get("path", "")
                base_dir = os.path.realpath(os.path.expanduser("~"))
                target_dir = os.path.realpath(os.path.expanduser(raw_path)) if raw_path else base_dir
                if not target_dir.startswith(base_dir) or not os.path.isdir(target_dir):
                    res["result"] = {"content": [{"type": "text", "text": "Invalid repository directory"}], "isError": True}
                else:
                    git_cmd = ["git", "status", "--short"] if tool_name == "git_status" else ["git", "diff", "--stat"]
                    try:
                        proc = subprocess.run(git_cmd, cwd=target_dir, capture_output=True, text=True, timeout=15)
                        output = proc.stdout if proc.returncode == 0 else (proc.stdout + "\n" + proc.stderr).strip()
                        res["result"] = {
                            "content": [{"type": "text", "text": output.strip() if output.strip() else "(clean working tree)"}],
                            "isError": proc.returncode != 0
                        }
                    except Exception as e:
                        res["result"] = {"content": [{"type": "text", "text": f"Git error: {str(e)}"}], "isError": True}

            else:
                res["error"] = {"code": -32601, "message": f"Tool '{tool_name}' not found or execution is restricted by policy."}

        else:
            res["error"] = {"code": -32601, "message": f"Method '{method}' not found"}

        # Return JSON-RPC response
        body_bytes = json.dumps(res, ensure_ascii=False).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body_bytes)))
        self.end_headers()
        self.wfile.write(body_bytes)

def main():
    http.server.ThreadingHTTPServer.allow_reuse_address = True
    server = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), McpRequestHandler)
    print("=" * 60)
    print(f"🚀 Ronin Termux MCP Server starting on http://127.0.0.1:{PORT}/mcp")
    if SHARED_SECRET:
        print("🔐 Security: HMAC-SHA256 Auth + Nonce Replay Guard ACTIVE")
    else:
        print("⚠️  Security Warning: RONIN_MCP_SECRET is not set in environment.")
        print("    Run: export RONIN_MCP_SECRET=\"<token from /mcp token>\"")
    print("=" * 60)
    print("Ready to receive requests from Ronin App. Press Ctrl+C to stop.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping Termux MCP Server...")
        server.server_close()

if __name__ == "__main__":
    main()
