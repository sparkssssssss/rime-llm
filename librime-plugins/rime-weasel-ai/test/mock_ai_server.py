#!/usr/bin/env python3
"""Mock OpenAI-compatible chat completion server for AI-correction testing.

Listens on 127.0.0.1 only. Validates that the request looks like an
OpenAI chat completion, then returns a fixed correction payload inside
choices[0].message.content (as JSON text), mimicking the expected model
output format.

Usage: python3 mock_ai_server.py [port]
"""
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != "/v1/chat/completions":
            self.send_error(404)
            return
        length = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(length).decode("utf-8", "replace")
        try:
            req = json.loads(raw)
        except json.JSONDecodeError:
            self.send_error(400, "bad json")
            return
        # Extract the user message to echo the pinyin back for testing
        user_content = ""
        for msg in req.get("messages", []):
            if msg.get("role") == "user":
                user_content = msg.get("content", "")
        first_line = user_content.splitlines()[0] if user_content else ""
        pinyin = first_line.replace("原始输入：", "").strip()

        payload = {
            "candidates": [
                {"text": f"[AI校准]{pinyin}", "score": 0.9}
            ]
        }
        content = json.dumps(payload, ensure_ascii=False)
        body = json.dumps({
            "id": "chatcmpl-mock",
            "object": "chat.completion",
            "choices": [{
                "index": 0,
                "message": {"role": "assistant", "content": content},
                "finish_reason": "stop",
            }],
        }).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        sys.stderr.write("mock_ai: " + fmt % args + "\n")


if __name__ == "__main__":
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    server = HTTPServer(("127.0.0.1", port), Handler)
    print(f"mock AI server on http://127.0.0.1:{port}/v1/chat/completions")
    server.serve_forever()
