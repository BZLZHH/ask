#!/usr/bin/env python3
import argparse
import json
import os
import pathlib
import pty
import select
import signal
import subprocess
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class Provider(BaseHTTPRequestHandler):
    def log_message(self, _format, *_args):
        pass

    def send_json(self, value, status=200):
        body = json.dumps(value).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def send_sse(self, events, delay=0.03):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        try:
            for event in events:
                data = event if isinstance(event, str) else json.dumps(event)
                frame = ("data: " + data + "\n\n").encode()
                split = max(1, len(frame) // 2)
                self.wfile.write(frame[:split])
                self.wfile.flush()
                self.wfile.write(frame[split:])
                self.wfile.flush()
                time.sleep(delay)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        if self.path == "/v1/models":
            self.send_json({"data": [{"id": "mock-model"}]})
        else:
            self.send_json({"error": {"message": "not found"}}, 404)

    def do_POST(self):
        length = int(self.headers.get("Content-Length", "0"))
        request = json.loads(self.rfile.read(length))
        if self.path == "/v1/messages":
            if request.get("stream"):
                self.send_sse([
                    {"type": "message_start", "message": {"usage": {"input_tokens": 8}}},
                    {"type": "content_block_start", "index": 0,
                     "content_block": {"type": "text", "text": ""}},
                    {"type": "content_block_delta", "index": 0,
                     "delta": {"type": "text_delta", "text": "anthropic "}},
                    {"type": "content_block_delta", "index": 0,
                     "delta": {"type": "text_delta", "text": "ok"}},
                    {"type": "message_delta", "delta": {"stop_reason": "end_turn"},
                     "usage": {"output_tokens": 2}},
                    {"type": "message_stop"},
                ])
                return
            self.send_json({
                "content": [{"type": "text", "text": "anthropic ok"}],
                "stop_reason": "end_turn",
                "usage": {"input_tokens": 8, "output_tokens": 2},
            })
            return
        if self.path.endswith(":streamGenerateContent?alt=sse"):
            self.send_sse([
                {"candidates": [{"content": {"role": "model", "parts": [{"text": "gemini "}]}}]},
                {"candidates": [{"content": {"role": "model", "parts": [{"text": "ok"}]},
                                  "finishReason": "STOP"}],
                 "usageMetadata": {"promptTokenCount": 7, "candidatesTokenCount": 2,
                                   "totalTokenCount": 9}},
            ])
            return
        if self.path.endswith(":generateContent"):
            self.send_json({
                "candidates": [{
                    "content": {"role": "model", "parts": [{"text": "gemini ok"}]},
                    "finishReason": "STOP",
                }],
                "usageMetadata": {
                    "promptTokenCount": 7,
                    "candidatesTokenCount": 2,
                    "totalTokenCount": 9,
                },
            })
            return
        messages = request.get("messages", [])
        pending_tools = set()
        invalid_tools = False
        for message in messages:
            role = message.get("role")
            if role == "assistant" and message.get("tool_calls"):
                if pending_tools:
                    invalid_tools = True
                    break
                pending_tools = {call.get("id") for call in message["tool_calls"]}
            elif role == "tool":
                call_id = message.get("tool_call_id")
                if call_id not in pending_tools:
                    invalid_tools = True
                    break
                pending_tools.remove(call_id)
            elif pending_tools:
                invalid_tools = True
                break
        if pending_tools:
            invalid_tools = True
        if invalid_tools:
            self.send_json({"error": {"message": "orphaned tool result"}}, 400)
            return
        system = next((m.get("content", "") for m in messages if m.get("role") == "system"), "")
        if request.get("stream"):
            last_role = messages[-1].get("role") if messages else ""
            user = next((m.get("content", "") for m in reversed(messages) if m.get("role") == "user"), "")
            if request.get("tools") and user == "exhaust tools":
                call_number = sum(1 for message in messages if message.get("role") == "tool")
                self.send_sse([
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": f"call-exhaust-{call_number}", "type": "function",
                        "function": {"name": "list_files", "arguments": "{}"},
                    }]}, "finish_reason": "tool_calls"}]},
                    "[DONE]",
                ])
            elif not request.get("tools") and user == "exhaust tools":
                self.send_sse([
                    {"choices": [{"delta": {"content": "budget "}, "finish_reason": None}]},
                    {"choices": [{"delta": {"content": "final"}, "finish_reason": "stop"}]},
                    "[DONE]",
                ])
            elif request.get("tools") and last_role == "user":
                self.send_sse([
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": "call-write", "type": "function",
                        "function": {"name": "write_file", "arguments": "{\"path\":\"made-"},
                    }]}, "finish_reason": None}]},
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "function": {
                            "arguments": "by-tool.txt\",\"content\":\"tool worked\"}"
                        },
                    }]}, "finish_reason": "tool_calls"}]},
                    "[DONE]",
                ])
            elif last_role == "tool":
                self.send_sse([
                    {"choices": [{"delta": {"content": "tool "}, "finish_reason": None}]},
                    {"choices": [{"delta": {"content": "complete"}, "finish_reason": "stop"}]},
                    {"choices": [], "usage": {"prompt_tokens": 10, "completion_tokens": 2,
                                               "total_tokens": 12}},
                    "[DONE]",
                ])
            elif user == "slow stream":
                self.send_sse([
                    {"choices": [{"delta": {"content": "partial"}, "finish_reason": None}]},
                    {"choices": [{"delta": {"content": " should-not-finish"},
                                  "finish_reason": "stop"}]},
                    "[DONE]",
                ], delay=1.0)
            else:
                self.send_sse([
                    {"choices": [{"delta": {"content": "echo: "}, "finish_reason": None}]},
                    {"choices": [{"delta": {"content": user}, "finish_reason": "stop"}]},
                    {"choices": [], "usage": {"prompt_tokens": 10, "completion_tokens": 2,
                                               "total_tokens": 12}},
                    "[DONE]",
                ])
            return
        if system.startswith("Create a compact"):
            content = "compact memory"
            message = {"role": "assistant", "content": content}
            finish = "stop"
        elif request.get("tools") and messages and messages[-1].get("role") == "user":
            message = {
                "role": "assistant",
                "content": None,
                "tool_calls": [{
                    "id": "call-write",
                    "type": "function",
                    "function": {
                        "name": "write_file",
                        "arguments": json.dumps({"path": "made-by-tool.txt", "content": "tool worked"}),
                    },
                }],
            }
            finish = "tool_calls"
        elif messages and messages[-1].get("role") == "tool":
            message = {"role": "assistant", "content": "tool complete"}
            finish = "stop"
        else:
            user = next((m.get("content", "") for m in reversed(messages) if m.get("role") == "user"), "")
            message = {"role": "assistant", "content": "echo: " + user}
            finish = "stop"
        self.send_json({
            "choices": [{"message": message, "finish_reason": finish}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12},
        })


class PtyProcess:
    def __init__(self, argv, cwd, env):
        self.master, slave = pty.openpty()
        self.process = subprocess.Popen(
            argv, cwd=cwd, env=env, stdin=slave, stdout=slave, stderr=slave,
            close_fds=True
        )
        os.close(slave)
        self.buffer = b""

    def send(self, data):
        os.write(self.master, data)

    def expect(self, needle, timeout=8):
        needle = needle.encode() if isinstance(needle, str) else needle
        deadline = time.monotonic() + timeout
        while needle not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"PTY timeout waiting for {needle!r}; output={self.buffer!r}")
            readable, _, _ = select.select([self.master], [], [], remaining)
            if not readable:
                continue
            try:
                chunk = os.read(self.master, 65536)
            except OSError:
                chunk = b""
            if not chunk:
                raise AssertionError(f"PTY exited waiting for {needle!r}; output={self.buffer!r}")
            self.buffer += chunk
        position = self.buffer.index(needle) + len(needle)
        consumed = self.buffer[:position]
        self.buffer = self.buffer[position:]
        return consumed

    def close(self):
        try:
            os.close(self.master)
        except OSError:
            pass


def exercise_repl(binary, root, env):
    terminal = PtyProcess([binary, "pty hello"], root, env)
    try:
        terminal.expect("echo: ")
        assert b"pty hello" not in terminal.buffer
        terminal.expect("pty hello")
        terminal.expect("ask> ")

        terminal.send(b"line one\\\n")
        terminal.expect("... ")
        terminal.send(b"line two\n")
        reply = terminal.expect("ask> ")
        assert b"echo: line one" in reply and b"line two" in reply, reply

        terminal.send(b"discard-me")
        terminal.send(b"\x03")
        time.sleep(0.1)
        terminal.send(b"kept\n")
        reply = terminal.expect("ask> ")
        assert b"echo: kept" in reply, reply

        terminal.send(b"slow stream\n")
        terminal.expect("partial")
        terminal.process.send_signal(signal.SIGINT)
        cancelled = terminal.expect("ask> ", timeout=3)
        assert b"request cancelled" in cancelled, cancelled
        assert b"should-not-finish" not in cancelled, cancelled

        terminal.send(b"!do make a file\n")
        reply = terminal.expect("ask> ")
        assert b"tool complete" in reply, reply
        assert (root / "made-by-tool.txt").read_text() == "tool worked"

        terminal.send(b"?printf shell-output\n")
        shell = terminal.expect("ask> ")
        assert b"shell-output" in shell and b"[exit 0]" in shell, shell

        terminal.send(b"!model\n")
        terminal.expect("select provider and model")
        terminal.send(b"\n")
        terminal.expect("ask> ")

        terminal.send(b"!config\n")
        terminal.expect("ask provider configuration")
        terminal.send(b"q")
        terminal.expect("Discard configuration changes?")
        terminal.send(b"y")
        terminal.expect("ask> ")

        terminal.send(b"!do exhaust tools\n")
        exhausted = terminal.expect("ask> ", timeout=8)
        assert b"tool round limit reached" in exhausted, exhausted
        assert b"budget final" in exhausted, exhausted

        terminal.send(b"!compact\n")
        compact = terminal.expect("ask> ")
        assert b"context compacted" in compact, compact
        terminal.send(b"after compact\n")
        after = terminal.expect("ask> ")
        assert b"echo: after compact" in after, after

        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        if terminal.process.poll() is None:
            terminal.process.kill()
            terminal.process.wait()
        terminal.close()

    eof = PtyProcess([binary], root, env)
    try:
        eof.expect("ask> ")
        eof.send(b"\x04")
        assert eof.process.wait(timeout=5) == 0
    finally:
        if eof.process.poll() is None:
            eof.process.kill()
            eof.process.wait()
        eof.close()

    configuration = PtyProcess([binary, "--config"], root, env)
    try:
        configuration.expect("ask provider configuration")
        configuration.send(b"s")
        assert configuration.process.wait(timeout=5) == 0
    finally:
        if configuration.process.poll() is None:
            configuration.process.kill()
            configuration.process.wait()
        configuration.close()

    resume = PtyProcess([binary, "resume"], root, env)
    try:
        resume.expect("resume conversation")
        resume.send(b"\n")
        resume.expect("ask> ")
        resume.send(b"!q\n")
        assert resume.process.wait(timeout=5) == 0
    finally:
        if resume.process.poll() is None:
            resume.process.kill()
            resume.process.wait()
        resume.close()


def protocol_config(protocol, base_url, model):
    return {
        "version": 1,
        "default_provider": protocol,
        "default_model": model,
        "providers": [{
            "id": protocol,
            "name": protocol,
            "protocol": protocol,
            "base_url": base_url,
            "api_key": "",
            "api_key_env": "",
            "models": [model],
            "default_model": model,
            "headers": {},
            "context_window": 8192,
            "timeout_seconds": 5,
            "enabled": True,
        }],
        "settings": {
            "auto_compact_ratio": 0.7,
            "max_tool_rounds": 4,
            "max_output_tokens": 512,
            "save_sessions": True,
            "system_prompt": "Test assistant",
        },
    }


def exercise_protocols_and_auto_compact(binary, server, root, env):
    for protocol, model, expected in (
        ("anthropic", "claude-mock", "anthropic ok\n"),
        ("gemini", "gemini-mock", "gemini ok\n"),
    ):
        config_home = root / (protocol + "-config")
        config_home.mkdir()
        base = f"http://127.0.0.1:{server.server_port}"
        if protocol == "gemini":
            base += "/v1beta"
        (config_home / "config.json").write_text(json.dumps(protocol_config(protocol, base, model)))
        protocol_env = env.copy()
        protocol_env["ASK_CONFIG_HOME"] = str(config_home)
        protocol_env["ASK_DATA_HOME"] = str(root / (protocol + "-data"))
        result = subprocess.run(
            [binary, "--no-repl", "test"], cwd=root, env=protocol_env,
            stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
        )
        assert result.stdout == expected, result

    compact_home = root / "compact-config"
    compact_home.mkdir()
    compact_config = protocol_config(
        "openai", f"http://127.0.0.1:{server.server_port}/v1", "mock-model"
    )
    compact_config["providers"][0]["context_window"] = 1024
    compact_config["settings"]["max_output_tokens"] = 256
    (compact_home / "config.json").write_text(json.dumps(compact_config))
    compact_env = env.copy()
    compact_env["ASK_CONFIG_HOME"] = str(compact_home)
    compact_env["ASK_DATA_HOME"] = str(root / "compact-data")
    terminal = PtyProcess([binary, "x" * 1350], root, compact_env)
    try:
        terminal.expect("ask> ")
        terminal.send(b"second\n")
        response = terminal.expect("ask> ")
        assert b"context reached the compact threshold" in response, response
        assert b"echo: second" in response, response
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        if terminal.process.poll() is None:
            terminal.process.kill()
            terminal.process.wait()
        terminal.close()


def run(binary):
    server = ThreadingHTTPServer(("127.0.0.1", 0), Provider)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix="ask-integration-") as directory:
            root = pathlib.Path(directory)
            config_home = root / "config"
            data_home = root / "data"
            config_home.mkdir()
            config = {
                "version": 1,
                "default_provider": "mock",
                "default_model": "mock-model",
                "providers": [{
                    "id": "mock",
                    "name": "Mock",
                    "protocol": "openai",
                    "base_url": f"http://127.0.0.1:{server.server_port}/v1",
                    "api_key": "",
                    "api_key_env": "",
                    "models": ["mock-model"],
                    "default_model": "mock-model",
                    "headers": {},
                    "context_window": 8192,
                    "timeout_seconds": 5,
                    "enabled": True,
                }],
                "settings": {
                    "auto_compact_ratio": 0.7,
                    "max_tool_rounds": 4,
                    "max_output_tokens": 512,
                    "save_sessions": True,
                    "system_prompt": "Test assistant",
                },
            }
            (config_home / "config.json").write_text(json.dumps(config))
            env = os.environ.copy()
            env["ASK_CONFIG_HOME"] = str(config_home)
            env["ASK_DATA_HOME"] = str(data_home)

            plain = subprocess.run(
                [binary, "--no-repl", "hello"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            assert plain.stdout == "echo: hello\n", plain

            no_stream = subprocess.run(
                [binary, "--no-repl", "--no-stream", "complete"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            assert no_stream.stdout == "echo: complete\n", no_stream

            piped = subprocess.run(
                [binary, "--no-repl", "prefix"], cwd=root, env=env,
                input="pipe body\n", text=True, capture_output=True, check=True
            )
            assert piped.stdout == "echo: prefix\n\npipe body\n", piped.stdout

            structured = subprocess.run(
                [binary, "--no-repl", "--json", "json test"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            payload = json.loads(structured.stdout)
            assert payload["text"] == "echo: json test"
            assert payload["usage"]["total_tokens"] == 12

            agent = subprocess.run(
                [binary, "--no-repl", "--do", "make a file"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            assert agent.stdout == "tool complete\n", agent
            assert (root / "made-by-tool.txt").read_text() == "tool worked"
            assert (data_home / "sessions.db").exists()
            exercise_repl(binary, root, env)
            exercise_protocols_and_auto_compact(binary, server, root, env)
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    args = parser.parse_args()
    run(os.path.abspath(args.binary))
