"""A small MCP client over stdio, for scripts and tests that drive the sidecar as an agent would."""
import base64
import json
import pathlib
import subprocess
import sys


class McpClient:
    def __init__(self, executable):
        script = pathlib.Path(__file__).parent / "diamond_dogs_mcp.py"
        self.process = subprocess.Popen([sys.executable, str(script), "--executable", str(executable)],
                                        stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8",
                                        bufsize=1)
        self.next_id = 1
        self.request("initialize", {"protocolVersion": "2025-06-18", "capabilities": {},
                                    "clientInfo": {"name": "mcp_client.py", "version": "1"}})
        self.notify("notifications/initialized")

    def notify(self, method):
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "method": method}) + "\n")
        self.process.stdin.flush()

    def request(self, method, params):
        request_id = self.next_id
        self.next_id += 1
        self.process.stdin.write(json.dumps({"jsonrpc": "2.0", "id": request_id, "method": method, "params": params}) + "\n")
        self.process.stdin.flush()
        reply = json.loads(self.process.stdout.readline())
        if reply.get("id") != request_id:
            raise RuntimeError(f"reply {reply.get('id')} to request {request_id}")
        if "error" in reply:
            raise RuntimeError(reply["error"])
        return reply["result"]

    def tool(self, name, arguments):
        """Returns (is_error, text content, image bytes or None)."""
        result = self.request("tools/call", {"name": name, "arguments": arguments})
        text = "\n".join(item["text"] for item in result["content"] if item["type"] == "text")
        images = [base64.b64decode(item["data"]) for item in result["content"] if item["type"] == "image"]
        return result["isError"], text, (images[0] if images else None)

    def json_tool(self, name, arguments):
        is_error, text, _ = self.tool(name, arguments)
        if is_error:
            raise RuntimeError(f"{name} failed: {text}")
        return json.loads(text)

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=120)
