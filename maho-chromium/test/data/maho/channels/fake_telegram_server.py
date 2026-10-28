#!/usr/bin/env python3
import argparse
from datetime import datetime
import http.server
import json
import re
import socketserver
import sys
import threading
import time
import urllib.parse


def log(msg: str) -> None:
    now = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    sys.stdout.write(f"[{now}] {msg}\n")
    sys.stdout.flush()


class TelegramState:
    def __init__(self):
        self.lock = threading.Condition()
        self.updates = []
        self.sent_messages = []
        self.next_update_id = 1
        self.next_message_id = 100

    def inject_message(self, chat_id, from_id, text, username=None):
        with self.lock:
            uid = self.next_update_id
            self.next_update_id += 1
            msg_id = self.next_message_id
            self.next_message_id += 1
            try:
                numeric_chat = int(chat_id)
            except (ValueError, TypeError):
                numeric_chat = 9999
            try:
                numeric_from = int(from_id)
            except (ValueError, TypeError):
                numeric_from = 8888

            update = {
                "update_id": uid,
                "message": {
                    "message_id": msg_id,
                    "date": int(time.time()),
                    "text": str(text),
                    "from": {
                        "id": numeric_from,
                        "username": username or f"user_{numeric_from}",
                        "is_bot": False,
                    },
                    "chat": {
                        "id": numeric_chat,
                        "type": "private",
                    },
                },
            }
            self.updates.append(update)
            log(f"_inject enqueued message update_id={uid} from={numeric_from} chat={numeric_chat} text={text!r}")
            self.lock.notify_all()
            return uid

    def get_updates(self, offset=None, timeout=0):
        deadline = time.time() + max(0, timeout)
        with self.lock:
            while True:
                available = []
                for u in self.updates:
                    if offset is None or u["update_id"] >= offset:
                        available.append(u)
                if available:
                    return available
                remaining = deadline - time.time()
                if remaining <= 0:
                    return []
                self.lock.wait(timeout=remaining)

    def record_sent_message(self, chat_id, text):
        with self.lock:
            msg_id = self.next_message_id
            self.next_message_id += 1
            record = {
                "message_id": msg_id,
                "chat_id": chat_id,
                "text": text,
                "timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            }
            self.sent_messages.append(record)
            log(f"sendMessage chat_id={chat_id} text={text!r}")
            return msg_id


STATE = TelegramState()


class FakeTelegramHandler(http.server.BaseHTTPRequestHandler):
    server_version = "FakeTelegramServer/1.0"

    def log_message(self, format, *args):
        log(f"HTTP {self.command} {self.path} - {format % args}")

    def send_json(self, status: int, payload: dict):
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def read_request_body(self) -> bytes:
        length = self.headers.get("Content-Length")
        if length:
            try:
                return self.rfile.read(int(length))
            except Exception as e:
                log(f"Error reading body: {e}")
        return b""

    def do_OPTIONS(self):
        self.send_response(204)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)

        if path in ("/health", "/_health", "/healthz"):
            self.send_json(200, {"ok": True, "status": "healthy"})
            return

        if path == "/_messages":
            with STATE.lock:
                self.send_json(200, {"ok": True, "messages": list(STATE.sent_messages)})
            return

        m_updates = re.match(r"^(?:/bot[^/]+)?/getUpdates$", path)
        if m_updates:
            offset = None
            if "offset" in query:
                try:
                    offset = int(query["offset"][0])
                except ValueError:
                    pass
            timeout = 0
            if "timeout" in query:
                try:
                    timeout = int(query["timeout"][0])
                except ValueError:
                    pass

            log(f"getUpdates polling offset={offset} timeout={timeout}")
            results = STATE.get_updates(offset=offset, timeout=timeout)
            log(f"getUpdates returning {len(results)} updates")
            self.send_json(200, {"ok": True, "result": results})
            return

        self.send_json(404, {"ok": False, "error_code": 404, "description": f"Not found: {path}"})

    def do_POST(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        query = urllib.parse.parse_qs(parsed.query)
        body_bytes = self.read_request_body()

        if path == "/_inject":
            try:
                data = json.loads(body_bytes.decode("utf-8")) if body_bytes else {}
            except Exception as e:
                self.send_json(400, {"ok": False, "error": f"Invalid JSON: {e}"})
                return

            chat_id = data.get("chat_id", 1001)
            from_id = data.get("from_id", 2001)
            text = data.get("text", "")
            username = data.get("username", None)
            if not text:
                self.send_json(400, {"ok": False, "error": "Missing 'text' field"})
                return

            update_id = STATE.inject_message(chat_id=chat_id, from_id=from_id, text=text, username=username)
            self.send_json(200, {"ok": True, "update_id": update_id})
            return

        m_send = re.match(r"^(?:/bot[^/]+)?/sendMessage$", path)
        if m_send:
            chat_id = None
            text = None
            content_type = self.headers.get("Content-Type", "")
            if "application/json" in content_type:
                try:
                    data = json.loads(body_bytes.decode("utf-8"))
                    chat_id = data.get("chat_id")
                    text = data.get("text")
                except Exception as e:
                    self.send_json(400, {"ok": False, "error": f"Invalid JSON: {e}"})
                    return
            else:
                form = urllib.parse.parse_qs(body_bytes.decode("utf-8", errors="replace"))
                if "chat_id" in form:
                    chat_id = form["chat_id"][0]
                if "text" in form:
                    text = form["text"][0]

            if chat_id is None and "chat_id" in query:
                chat_id = query["chat_id"][0]
            if text is None and "text" in query:
                text = query["text"][0]

            if chat_id is None or text is None:
                self.send_json(400, {"ok": False, "error_code": 400, "description": "Missing chat_id or text"})
                return

            try:
                numeric_chat = int(chat_id)
            except (ValueError, TypeError):
                numeric_chat = 9999

            msg_id = STATE.record_sent_message(chat_id=chat_id, text=text)
            response = {
                "ok": True,
                "result": {
                    "message_id": msg_id,
                    "from": {"id": 12345678, "is_bot": True, "first_name": "MahoFakeBot"},
                    "chat": {"id": numeric_chat, "type": "private"},
                    "date": int(time.time()),
                    "text": str(text),
                },
            }
            self.send_json(200, response)
            return

        m_updates = re.match(r"^(?:/bot[^/]+)?/getUpdates$", path)
        if m_updates:
            offset = None
            timeout = 0
            if "application/json" in self.headers.get("Content-Type", "") and body_bytes:
                try:
                    data = json.loads(body_bytes.decode("utf-8"))
                    offset = data.get("offset")
                    timeout = data.get("timeout", 0)
                except Exception:
                    pass
            if offset is None and "offset" in query:
                try:
                    offset = int(query["offset"][0])
                except ValueError:
                    pass
            if not timeout and "timeout" in query:
                try:
                    timeout = int(query["timeout"][0])
                except ValueError:
                    pass

            log(f"POST getUpdates polling offset={offset} timeout={timeout}")
            results = STATE.get_updates(offset=offset, timeout=timeout)
            log(f"POST getUpdates returning {len(results)} updates")
            self.send_json(200, {"ok": True, "result": results})
            return

        self.send_json(404, {"ok": False, "error_code": 404, "description": f"Not found: {path}"})


class ThreadedHTTPServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def main():
    parser = argparse.ArgumentParser(description="Test-only fake Telegram Bot API HTTP server")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=8080, help="Bind port (default: 8080, 0 for dynamic)")
    args = parser.parse_args()

    server = ThreadedHTTPServer((args.host, args.port), FakeTelegramHandler)
    actual_port = server.server_address[1]
    log(f"Fake Telegram Server listening at http://{args.host}:{actual_port}")
    log("Ready for getUpdates / sendMessage requests and POST /_inject admin messages.")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        log("Shutting down server...")
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
