#!/usr/bin/env python3
"""
Ronin Kernel - Reference Termux MCP Server Implementation.
Exposes safe Termux tools via JSON-RPC 2.0 with HMAC-SHA256 signature verification.
"""

import http.server
import json
import subprocess
import os
import hmac
import hashlib
import time

PORT = 8765
SHARED_SECRET = os.environ.get("RONIN_MCP_SECRET", "").strip()

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

        # Security Verification (HMAC-SHA256 & Replay Window)
        timestamp_str = self.headers.get("X-Ronin-Timestamp", "")
        signature = self.headers.get("X-Ronin-Signature", "")

        if SHARED_SECRET:
            try:
                req_time = int(timestamp_str) if timestamp_str else 0
                now = int(time.time())
                if abs(now - req_time) > 60:
                    self.send_error(401, f"Timestamp replay window expired (clock drift: {abs(now - req_time)}s)")
                    return
                expected_sig = hmac.new(
                    SHARED_SECRET.encode("utf-8"),
                    f"{req_time}.{raw_body}".encode("utf-8"),
                    hashlib.sha256
                ).hexdigest()
                if not hmac.compare_digest(expected_sig.lower(), signature.lower()):
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
                "serverInfo": {"name": "termux-mcp", "version": "1.0"},
                "capabilities": {"tools": {}}
            }

        # 2. List Available Tools
        elif method == "tools/list":
            res["result"] = {
                "tools": [
                    {
                        "name": "run_bash",
                        "description": "Execute a shell command in Termux environment",
                        "inputSchema": {
                            "type": "object",
                            "properties": {
                                "command": {"type": "string", "description": "Bash command line to execute"}
                            },
                            "required": ["command"]
                        }
                    },
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
                                "path": {"type": "string", "description": "Directory path"}
                            }
                        }
                    }
                ]
            }

        # 3. Call Tool (Executed only after Ronin HITL approval)
        elif method == "tools/call":
            tool_name = params.get("name")
            args = params.get("arguments", {})

            if tool_name == "run_bash":
                cmd = args.get("command", "echo 'No command provided'")
                try:
                    proc = subprocess.run(cmd, shell=True, capture_output=True, text=True, timeout=30)
                    output = proc.stdout if proc.returncode == 0 else (proc.stdout + "\n" + proc.stderr).strip()
                    res["result"] = {
                        "content": [{"type": "text", "text": output.strip() if output.strip() else "(command executed with no output)"}],
                        "isError": proc.returncode != 0
                    }
                except Exception as e:
                    res["result"] = {"content": [{"type": "text", "text": f"Execution error: {str(e)}"}], "isError": True}

            elif tool_name == "get_device_info":
                uname = subprocess.getoutput("uname -a")
                uptime = subprocess.getoutput("uptime")
                whoami = subprocess.getoutput("whoami")
                info_text = f"User: {whoami}\nSystem: {uname}\nUptime: {uptime}"
                res["result"] = {"content": [{"type": "text", "text": info_text}], "isError": False}

            elif tool_name == "list_files":
                target_dir = args.get("path", os.path.expanduser("~"))
                try:
                    entries = os.listdir(target_dir)
                    res["result"] = {"content": [{"type": "text", "text": "\n".join(entries[:50])}], "isError": False}
                except Exception as e:
                    res["result"] = {"content": [{"type": "text", "text": f"Error: {str(e)}"}], "isError": True}

            else:
                res["error"] = {"code": -32601, "message": f"Tool '{tool_name}' not found"}

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
        masked = SHARED_SECRET[:8] + "..." + SHARED_SECRET[-6:]
        print(f"🔐 Security: HMAC-SHA256 Auth ACTIVE (Secret: {masked})")
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
