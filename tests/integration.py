#!/usr/bin/env python3
import argparse
import fcntl
import json
import os
import pathlib
import pty
import select
import signal
import struct
import subprocess
import tempfile
import termios
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
        with self.server.requests_lock:
            self.server.requests.append((self.path, request))
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
        tool_names = {
            tool.get("function", {}).get("name") for tool in request.get("tools", [])
        }
        last_tool_name = ""
        for item in reversed(messages):
            if item.get("role") == "assistant" and item.get("tool_calls"):
                last_tool_name = item["tool_calls"][0].get("function", {}).get("name", "")
                break
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
            elif last_role == "user" and user == "read workspace":
                self.send_sse([
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": "call-read", "type": "function",
                        "function": {"name": "read_file",
                                     "arguments": "{\"path\":\"readonly-source.txt\"}"},
                    }]}, "finish_reason": "tool_calls"}]},
                    "[DONE]",
                ])
            elif last_role == "user" and user.startswith("upgrade ") and "request_do_mode" in tool_names:
                scope = "conversation" if user == "upgrade conversation" else "once"
                arguments = json.dumps({
                    "reason": "The requested task needs a file change",
                    "operation": "Create the approved marker file",
                    "suggested_scope": scope,
                })
                self.send_sse([
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": "call-upgrade", "type": "function",
                        "function": {"name": "request_do_mode", "arguments": arguments},
                    }]}, "finish_reason": "tool_calls"}]},
                    "[DONE]",
                ])
            elif last_role == "user" and "write_file" in tool_names:
                target = "conversation-second.txt" if user == "conversation second write" \
                    else "made-by-tool.txt"
                self.send_sse([
                    {"choices": [{"delta": {"tool_calls": [{
                        "index": 0, "id": "call-write", "type": "function",
                        "function": {"name": "write_file", "arguments": json.dumps({
                            "path": target, "content": "tool worked"
                        })},
                    }]}, "finish_reason": "tool_calls"}]},
                    "[DONE]",
                ])
            elif last_role == "tool" and last_tool_name == "request_do_mode":
                granted = messages[-1].get("content", "").find('"ok":true') >= 0
                if granted and "write_file" in tool_names:
                    grant = "conversation" if \
                        messages[-1].get("content", "").find('"conversation"') >= 0 \
                        else "once"
                    self.send_sse([
                        {"choices": [{"delta": {"tool_calls": [{
                            "index": 0, "id": "call-approved-write", "type": "function",
                            "function": {"name": "write_file", "arguments": json.dumps({
                                "path": f"approved-{grant}.txt", "content": "approved"
                            })},
                        }]}, "finish_reason": "tool_calls"}]},
                        "[DONE]",
                    ])
                else:
                    self.send_sse([
                        {"choices": [{"delta": {"content": "permission denied"},
                                      "finish_reason": "stop"}]},
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
        if system.startswith("You classify whether a terminal user"):
            judge_input = next((m.get("content", "") for m in messages
                                if m.get("role") == "user"), "")
            if "judge failure" in judge_input:
                self.send_json({"error": {"message": "judge unavailable"}}, 500)
                return
            if "judge continue" in judge_input:
                content = "CONTINUE"
            elif "judge labelled continue" in judge_input:
                content = "Decision: CONTINUE. EXIT is not selected because a follow-up is likely."
            elif "judge json exit" in judge_input:
                content = '{"decision":"EXIT","reason":"The exchange is complete."}'
            elif "sudo rm -rf / and do dd of=/dev/* if=/dev/zero" in judge_input:
                content = "Decision: CONTINUE. The request is dangerous; explain the risk before EXIT."
            elif "judge wrapped exit" in judge_input:
                content = "```text\nDecision: EXIT.\n```"
            elif "judge ambiguous" in judge_input:
                content = "Use CONTINUE rather than EXIT"
            elif "judge invalid" in judge_input:
                content = "MAYBE"
            else:
                content = "EXIT"
            message = {"role": "assistant", "content": content}
            finish = "stop"
        elif system.startswith("Create a compact"):
            content = "compact memory"
            message = {"role": "assistant", "content": content}
            finish = "stop"
        elif messages and messages[-1].get("role") == "user":
            user = next((m.get("content", "") for m in reversed(messages)
                         if m.get("role") == "user"), "")
            if user == "read workspace" and "read_file" in tool_names:
                message = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [{
                        "id": "call-read",
                        "type": "function",
                        "function": {
                            "name": "read_file",
                            "arguments": json.dumps({"path": "readonly-source.txt"}),
                        },
                    }],
                }
                finish = "tool_calls"
            elif user.startswith("upgrade " ) and "request_do_mode" in tool_names:
                scope = "conversation" if user == "upgrade conversation" else "once"
                message = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [{
                        "id": "call-upgrade",
                        "type": "function",
                        "function": {
                            "name": "request_do_mode",
                            "arguments": json.dumps({
                                "reason": "The requested task needs a file change",
                                "operation": "Create the approved marker file",
                                "suggested_scope": scope,
                            }),
                        },
                    }],
                }
                finish = "tool_calls"
            elif "write_file" in tool_names:
                target = "conversation-second.txt" if user == "conversation second write" \
                    else "made-by-tool.txt"
                message = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [{
                        "id": "call-write",
                        "type": "function",
                        "function": {
                            "name": "write_file",
                            "arguments": json.dumps({"path": target, "content": "tool worked"}),
                        },
                    }],
                }
                finish = "tool_calls"
            else:
                message = {"role": "assistant", "content": "echo: " + user}
                finish = "stop"
        elif messages and messages[-1].get("role") == "tool" and \
                last_tool_name == "request_do_mode":
            granted = messages[-1].get("content", "").find('"ok":true') >= 0
            if granted and "write_file" in tool_names:
                grant = "conversation" if \
                    messages[-1].get("content", "").find('"conversation"') >= 0 \
                    else "once"
                message = {
                    "role": "assistant",
                    "content": None,
                    "tool_calls": [{
                        "id": "call-approved-write",
                        "type": "function",
                        "function": {
                            "name": "write_file",
                            "arguments": json.dumps({
                                "path": f"approved-{grant}.txt", "content": "approved"
                            }),
                        },
                    }],
                }
                finish = "tool_calls"
            else:
                message = {"role": "assistant", "content": "permission denied"}
                finish = "stop"
        elif messages and messages[-1].get("role") == "tool":
            message = {"role": "assistant", "content": "tool complete"}
            finish = "stop"
        else:
            user = next((m.get("content", "") for m in reversed(messages)
                         if m.get("role") == "user"), "")
            message = {"role": "assistant", "content": "echo: " + user}
            finish = "stop"
        self.send_json({
            "choices": [{"message": message, "finish_reason": finish}],
            "usage": {"prompt_tokens": 10, "completion_tokens": 2, "total_tokens": 12},
        })


class PtyProcess:
    def __init__(self, argv, cwd, env):
        self.master, slave = pty.openpty()
        fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 24, 80, 0, 0))
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
        terminal.expect("ask settings")
        terminal.send(b"\x1b")
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
        down = b"\x1bOB"
        configuration.expect("ask settings")
        configuration.send(b"\n")
        configuration.expect("➔ Default model")
        configuration.send(b"\x1b")
        configuration.expect("Connections")
        configuration.send(down * 5 + b"\n")
        configuration.expect("➔ AI call")
        configuration.expect("Thinking strength")
        configuration.send(b"\x1bOC")
        configuration.expect("Off")
        configuration.send(down + b"\x1bOC")
        configuration.expect("256 tokens")
        configuration.send(down + b"\x1bOC")
        configuration.expect("0.00")
        configuration.send(down + b"\x1bOC")
        configuration.expect("0.00")
        configuration.send(down + b"\x1bOC")
        configuration.expect("768")
        configuration.send(down + b"\x1bOC")
        configuration.expect("ff")
        configuration.send(down + b"\n")
        configuration.expect("➔ Advanced request JSON")
        configuration.send(b"\x7f" * len('{"seed":7}') + b"[\n")
        configuration.expect("must be a valid JSON object")
        configuration.send(b"\x1b")
        configuration.expect("Connections")
        configuration.send(down + b"\n")
        configuration.expect("➔ Conversation entry")
        configuration.send(b"\x1bOD")
        configuration.expect("Automatic")
        configuration.send(down + b"\n")
        configuration.expect("➔ Judge model")
        configuration.send(down + b"\n")
        configuration.expect("mock / mock-model-2")
        configuration.send(b"\x1b")
        configuration.expect("Connections")
        configuration.send(down + b"\n")
        configuration.expect("➔ Providers")
        configuration.send(b"\n")
        configuration.expect("➔ Mock")
        configuration.send(down * 7 + b"\n")
        configuration.expect("Provider default: mock-model")
        configuration.send(down + b"\n")
        configuration.expect("Set as provider default")
        configuration.send(b"\n")
        configuration.expect("default: mock-model-2")
        configuration.send(b"\x1b")
        configuration.expect("Connection, authentication and model settings")
        configuration.send(b"\x1b")
        configuration.expect("Add provider")
        configuration.send(b"\x1b")
        configuration.expect("General")
        configuration.send(down * 2 + b"\n")
        assert configuration.process.wait(timeout=5) == 0
        saved = json.loads((pathlib.Path(env["ASK_CONFIG_HOME"]) / "config.json").read_text())
        assert saved["default_model"] == "mock-model-2", saved
        assert saved["providers"][0]["default_model"] == "mock-model-2", saved
        assert saved["settings"]["reasoning_effort"] == "off", saved
        assert saved["settings"]["thinking_budget_tokens"] == 256, saved
        assert saved["settings"]["temperature"] == 0.0, saved
        assert saved["settings"]["top_p"] == 0.0, saved
        assert saved["settings"]["max_output_tokens"] == 768, saved
        assert saved["settings"]["stream_output"] is False, saved
        assert saved["settings"]["custom_parameters"] == {"seed": 7}, saved
        assert saved["settings"]["conversation_entry_mode"] == "automatic", saved
        assert saved["settings"]["judge_provider"] == "mock", saved
        assert saved["settings"]["judge_model"] == "mock-model-2", saved
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


def request_slice(server, before):
    with server.requests_lock:
        return server.requests[before:]


def request_count(server):
    with server.requests_lock:
        return len(server.requests)


def entry_policy_environment(root, env, server, name, mode):
    config_home = root / (name + "-config")
    data_home = root / (name + "-data")
    config_home.mkdir()
    config = protocol_config(
        "openai", f"http://127.0.0.1:{server.server_port}/v1", "mock-model"
    )
    config["providers"][0].update({
        "id": "mock",
        "name": "Mock",
        "models": ["mock-model", "mock-model-2"],
        "default_model": "mock-model",
    })
    config["default_provider"] = "mock"
    config["settings"].update({
        "conversation_entry_mode": mode,
        "judge_provider": "mock",
        "judge_model": "mock-model-2",
    })
    (config_home / "config.json").write_text(json.dumps(config))
    policy_env = env.copy()
    policy_env["ASK_CONFIG_HOME"] = str(config_home)
    policy_env["ASK_DATA_HOME"] = str(data_home)
    return policy_env, data_home


def assert_judge_request(request, original_prompt):
    path, body = request
    assert path == "/v1/chat/completions", request
    assert body["model"] == "mock-model-2", body
    assert body["stream"] is False, body
    assert body["max_tokens"] == 128, body
    assert body["temperature"] == 0.0, body
    assert "reasoning_effort" not in body, body
    assert "tools" not in body and "tool_choice" not in body, body
    assert len(body["messages"]) == 2, body
    assert body["messages"][0]["role"] == "system", body
    assert body["messages"][0]["content"].startswith(
        "You classify whether a terminal user"
    ), body
    assert "When uncertain, prefer CONTINUE" in body["messages"][0]["content"], body
    assert "Output exactly one token: CONTINUE or EXIT" in body["messages"][0]["content"], body
    assert body["messages"][1]["role"] == "user", body
    assert "<conversation_context>" in body["messages"][1]["content"], body
    assert "mode: read-only" in body["messages"][1]["content"], body
    assert "tool_calls_used: no" in body["messages"][1]["content"], body
    assert original_prompt in body["messages"][1]["content"], body
    assert "echo: " + original_prompt in body["messages"][1]["content"], body


def stop_pty(terminal):
    if terminal.process.poll() is None:
        terminal.process.kill()
        terminal.process.wait()
    terminal.close()


def permission_environment(root, env, server, name, entry_mode="always_continue"):
    workspace = root / (name + "-workspace")
    config_home = root / (name + "-config")
    data_home = root / (name + "-data")
    workspace.mkdir()
    config_home.mkdir()
    (workspace / "readonly-source.txt").write_text("read-only fixture\n")
    config = protocol_config(
        "openai", f"http://127.0.0.1:{server.server_port}/v1", "mock-model"
    )
    config["settings"].update({
        "conversation_entry_mode": entry_mode,
        "stream_output": True,
    })
    (config_home / "config.json").write_text(json.dumps(config))
    permission_env = env.copy()
    permission_env["ASK_CONFIG_HOME"] = str(config_home)
    permission_env["ASK_DATA_HOME"] = str(data_home)
    return workspace, permission_env, data_home


def request_tool_names(request):
    return {
        tool.get("function", {}).get("name")
        for tool in request[1].get("tools", [])
    }


def request_system_prompt(request):
    path, body = request
    if path == "/v1/chat/completions":
        systems = [message.get("content", "") for message in body.get("messages", [])
                   if message.get("role") == "system"]
        assert len(systems) == 1, body
        return systems[0]
    if path == "/v1/messages":
        return body.get("system", "")
    if "GenerateContent" in path or "generateContent" in path:
        parts = body.get("systemInstruction", {}).get("parts", [])
        assert len(parts) == 1, body
        return parts[0].get("text", "")
    raise AssertionError(request)


def assert_permission_context(request, state):
    prompt = request_system_prompt(request)
    assert prompt.startswith("Test assistant\n\n[ask runtime permissions]\n"), prompt
    assert f"Current permission state: {state}" in prompt, prompt
    assert prompt.endswith("[end ask runtime permissions]"), prompt
    if state == "ASK_READ_ONLY":
        assert "Full DO mode would additionally provide:" in prompt, prompt
        assert "write_file, run_command, fetch_http, browse_page, web_search" in prompt, prompt
        assert "Deny, Allow once, or Allow for conversation" in prompt, prompt
    elif state == "DO_ONCE_THIS_RESPONSE":
        assert "complete tool-call batch" in prompt, prompt
        assert "consumed when this response is returned" in prompt, prompt
        assert "later model response returns to ASK_READ_ONLY" in prompt, prompt
    elif state == "DO_FOR_CONVERSATION":
        assert "quick-resumed, or explicitly resumed" in prompt, prompt
        assert "does not change global configuration" in prompt, prompt
    elif state == "DO_FOR_USER_TURN":
        assert "user explicitly used !do" in prompt, prompt
        assert "next user turn returns" in prompt, prompt
    elif state == "FORCED_ASK_READ_ONLY":
        assert "user explicitly used !ask" in prompt, prompt
        assert "cannot request or obtain DO mode" in prompt, prompt
    if request[0] == "/v1/chat/completions":
        non_system = [message.get("content") for message in request[1].get("messages", [])
                      if message.get("role") != "system"]
        assert all("[ask runtime permissions]" not in (content or "")
                   for content in non_system), request[1]


def tool_result_data(request, tool_name):
    messages = request[1].get("messages", [])
    call_ids = {}
    for message in messages:
        for call in message.get("tool_calls", []):
            call_ids[call.get("id")] = call.get("function", {}).get("name")
    for message in reversed(messages):
        if message.get("role") == "tool" and \
                call_ids.get(message.get("tool_call_id")) == tool_name:
            return json.loads(message.get("content", "{}"))
    raise AssertionError((tool_name, request))


def assert_readonly_tools(request, escalation=True):
    names = request_tool_names(request)
    expected = {"read_file", "list_files", "search_text", "run_readonly_command"}
    assert expected <= names, names
    assert "write_file" not in names and "run_command" not in names, names
    assert ("request_do_mode" in names) == escalation, names


def assert_full_tools(request):
    names = request_tool_names(request)
    assert {"read_file", "write_file", "run_command", "web_search"} <= names, names
    assert "request_do_mode" not in names, names


def exercise_permissions(binary, server, root, env):
    workspace, read_env, _ = permission_environment(root, env, server, "permission-read")
    before = request_count(server)
    result = subprocess.run(
        [binary, "--no-repl", "read workspace"], cwd=workspace, env=read_env,
        stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
    )
    assert result.stdout == "tool complete\n", result
    requests = request_slice(server, before)
    assert len(requests) == 2, requests
    assert_readonly_tools(requests[0])
    assert_readonly_tools(requests[1])
    assert_permission_context(requests[0], "ASK_READ_ONLY")
    assert_permission_context(requests[1], "ASK_READ_ONLY")

    workspace, deny_env, _ = permission_environment(root, env, server, "permission-deny")
    before = request_count(server)
    terminal = PtyProcess([binary, "upgrade once"], workspace, deny_env)
    try:
        terminal.expect("ask permission ➔ Do mode")
        terminal.send(b"\x1b")
        output = terminal.expect("ask> ")
        assert b"permission denied" in output, output
        assert not (workspace / "approved-once.txt").exists()
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    requests = request_slice(server, before)
    assert len(requests) == 2, requests
    assert_readonly_tools(requests[0])
    assert_readonly_tools(requests[1])
    assert_permission_context(requests[0], "ASK_READ_ONLY")
    assert_permission_context(requests[1], "ASK_READ_ONLY")
    denied_data = tool_result_data(requests[1], "request_do_mode")
    assert denied_data["data"]["permission_state"] == "ASK_READ_ONLY", denied_data
    assert denied_data["data"]["granted"] == "deny", denied_data

    workspace, once_env, _ = permission_environment(root, env, server, "permission-once")
    before = request_count(server)
    terminal = PtyProcess([binary, "upgrade once"], workspace, once_env)
    try:
        terminal.expect("ask permission ➔ Do mode")
        terminal.expect(b"> Deny")
        terminal.send(b"\x1bOB")
        terminal.expect(b"> Allow once")
        terminal.send(b"\n")
        output = terminal.expect("ask> ")
        assert b"one-time do mode consumed" in output, output
        assert (workspace / "approved-once.txt").read_text() == "approved"
        terminal.send(b"after once\n")
        terminal.expect("ask> ")
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    requests = request_slice(server, before)
    assert len(requests) == 4, requests
    assert_readonly_tools(requests[0])
    assert_full_tools(requests[1])
    assert_readonly_tools(requests[2])
    assert_readonly_tools(requests[3])
    assert_permission_context(requests[0], "ASK_READ_ONLY")
    assert_permission_context(requests[1], "DO_ONCE_THIS_RESPONSE")
    assert_permission_context(requests[2], "ASK_READ_ONLY")
    assert_permission_context(requests[3], "ASK_READ_ONLY")
    once_data = tool_result_data(requests[1], "request_do_mode")
    assert once_data["data"]["permission_state"] == "DO_ONCE_NEXT_RESPONSE", once_data
    assert once_data["data"]["applies_to"] == (
        "next model response and its complete tool-call batch"
    ), once_data
    assert once_data["data"]["after_consumption"] == "ASK_READ_ONLY", once_data
    assert once_data["data"]["persisted"] is False, once_data

    workspace, conversation_env, _ = permission_environment(
        root, env, server, "permission-conversation"
    )
    before = request_count(server)
    terminal = PtyProcess([binary, "upgrade conversation"], workspace, conversation_env)
    try:
        terminal.expect("ask permission ➔ Do mode")
        terminal.expect(b"> Deny")
        terminal.send(b"\x1bOB")
        terminal.expect(b"> Allow once")
        terminal.send(b"\x1bOB")
        terminal.expect(b"> Allow for conversation")
        terminal.send(b"\n")
        terminal.expect("do> ")
        assert (workspace / "approved-conversation.txt").read_text() == "approved"
        terminal.send(b"conversation second write\n")
        terminal.expect("do> ")
        assert (workspace / "conversation-second.txt").read_text() == "tool worked"
        terminal.send(b"!ask conversation second write\n")
        terminal.expect("do> ")
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    requests = request_slice(server, before)
    assert len(requests) == 6, requests
    assert_readonly_tools(requests[0])
    assert_full_tools(requests[1])
    assert_full_tools(requests[2])
    assert_full_tools(requests[3])
    assert_full_tools(requests[4])
    assert_readonly_tools(requests[5], escalation=False)
    assert_permission_context(requests[0], "ASK_READ_ONLY")
    for request in requests[1:5]:
        assert_permission_context(request, "DO_FOR_CONVERSATION")
    assert_permission_context(requests[5], "FORCED_ASK_READ_ONLY")
    conversation_data = tool_result_data(requests[1], "request_do_mode")
    assert conversation_data["data"]["permission_state"] == (
        "DO_FOR_CONVERSATION"
    ), conversation_data
    assert conversation_data["data"]["persisted"] is True, conversation_data

    before = request_count(server)
    resumed = PtyProcess([binary, "resume"], workspace, conversation_env)
    try:
        resumed.expect("resume conversation")
        resumed.send(b"\n")
        resumed.expect("[do]")
        resumed.expect("do> ")
        resumed.send(b"after explicit resume\n")
        resumed.expect("do> ")
        resumed.send(b"!q\n")
        assert resumed.process.wait(timeout=5) == 0
    finally:
        stop_pty(resumed)
    resumed_requests = request_slice(server, before)
    assert len(resumed_requests) == 2, resumed_requests
    for request in resumed_requests:
        assert_full_tools(request)
        assert_permission_context(request, "DO_FOR_CONVERSATION")

    workspace, non_tty_env, _ = permission_environment(root, env, server, "permission-non-tty")
    before = request_count(server)
    denied = subprocess.run(
        [binary, "--no-repl", "upgrade once"], cwd=workspace, env=non_tty_env,
        stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
    )
    assert denied.stdout == "permission denied\n", denied
    assert not (workspace / "approved-once.txt").exists()
    requests = request_slice(server, before)
    assert len(requests) == 2, requests
    assert_readonly_tools(requests[0])
    assert_readonly_tools(requests[1])
    assert_permission_context(requests[0], "ASK_READ_ONLY")
    assert_permission_context(requests[1], "ASK_READ_ONLY")

    workspace, one_turn_env, _ = permission_environment(root, env, server, "permission-do-turn")
    before = request_count(server)
    terminal = PtyProcess([binary], workspace, one_turn_env)
    try:
        terminal.expect("ask> ")
        terminal.send(b"!do conversation second write\n")
        terminal.expect("ask> ")
        terminal.send(b"after do turn\n")
        terminal.expect("ask> ")
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    requests = request_slice(server, before)
    assert len(requests) == 3, requests
    assert_full_tools(requests[0])
    assert_full_tools(requests[1])
    assert_readonly_tools(requests[2])
    assert_permission_context(requests[0], "DO_FOR_USER_TURN")
    assert_permission_context(requests[1], "DO_FOR_USER_TURN")
    assert_permission_context(requests[2], "ASK_READ_ONLY")

    workspace, quick_env, _ = permission_environment(
        root, env, server, "permission-quick", "always_exit"
    )
    terminal = PtyProcess([binary, "upgrade conversation"], workspace, quick_env)
    try:
        terminal.expect("ask permission ➔ Do mode")
        terminal.expect(b"> Deny")
        terminal.send(b"\x1bOB")
        terminal.expect(b"> Allow once")
        terminal.send(b"\x1bOB")
        terminal.expect(b"> Allow for conversation")
        terminal.send(b"\n")
        terminal.expect("tool complete")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    before = request_count(server)
    resumed = PtyProcess([binary], workspace, quick_env)
    try:
        resumed.expect("ask: resumed ")
        resumed.expect("[do]")
        resumed.expect("do> ")
        resumed.send(b"after quick resume\n")
        resumed.expect("do> ")
        resumed.send(b"!q\n")
        assert resumed.process.wait(timeout=5) == 0
    finally:
        stop_pty(resumed)
    resumed_requests = request_slice(server, before)
    assert len(resumed_requests) == 2, resumed_requests
    for request in resumed_requests:
        assert_full_tools(request)
        assert_permission_context(request, "DO_FOR_CONVERSATION")


def exercise_entry_policy(binary, server, root, env):
    continue_env, _ = entry_policy_environment(
        root, env, server, "entry-always-continue", "always_continue"
    )
    before = request_count(server)
    terminal = PtyProcess([binary, "always continue"], root, continue_env)
    try:
        terminal.expect("ask> ")
        assert len(request_slice(server, before)) == 1, request_slice(server, before)
        terminal.send(b"!q\n")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)

    deepseek_env, _ = entry_policy_environment(
        root, env, server, "entry-deepseek-judge", "automatic"
    )
    deepseek_config_path = pathlib.Path(deepseek_env["ASK_CONFIG_HOME"]) / "config.json"
    deepseek_config = json.loads(deepseek_config_path.read_text())
    deepseek_config["providers"][0]["id"] = "deepseek"
    deepseek_config["providers"][0]["name"] = "DeepSeek"
    deepseek_config["default_provider"] = "deepseek"
    deepseek_config["settings"]["judge_provider"] = "deepseek"
    deepseek_config_path.write_text(json.dumps(deepseek_config))
    before = request_count(server)
    terminal = PtyProcess([binary, "judge exit deepseek"], root, deepseek_env)
    try:
        terminal.expect("echo: judge exit deepseek")
        assert terminal.process.wait(timeout=5) == 0
        requests = request_slice(server, before)
        assert len(requests) == 2, requests
        assert requests[1][1]["reasoning_effort"] == "low", requests[1]
    finally:
        stop_pty(terminal)

    exit_env, exit_data = entry_policy_environment(
        root, env, server, "entry-always-exit", "always_exit"
    )
    before = request_count(server)
    terminal = PtyProcess([binary, "always exit"], root, exit_env)
    try:
        terminal.expect("echo: always exit")
        assert terminal.process.wait(timeout=5) == 0
        assert len(request_slice(server, before)) == 1, request_slice(server, before)
        assert (exit_data / "quick-resume.json").exists()
    finally:
        stop_pty(terminal)

    resumed = PtyProcess([binary], root, exit_env)
    try:
        resumed.expect("ask: resumed ")
        resumed.expect("[ask/read-only]")
        resumed.expect("ask> ")
        resumed.send(b"!q\n")
        assert resumed.process.wait(timeout=5) == 0
    finally:
        stop_pty(resumed)

    consumed = PtyProcess([binary], root, exit_env)
    try:
        fresh = consumed.expect("ask> ")
        assert b"resumed" not in fresh, fresh
        consumed.send(b"!q\n")
        assert consumed.process.wait(timeout=5) == 0
    finally:
        stop_pty(consumed)

    for prompt, expect_repl, expect_error in (
        ("judge exit", False, False),
        ("judge wrapped exit", False, False),
        ("judge labelled continue", True, False),
        ("judge json exit", False, False),
        ("sudo rm -rf / and do dd of=/dev/* if=/dev/zero", True, False),
        ("judge continue", True, False),
        ("judge ambiguous", True, True),
        ("judge invalid", True, True),
        ("judge failure", True, True),
    ):
        name = prompt.replace(" ", "-").replace("/", "-").replace("*", "-").replace("=", "-")
        policy_env, policy_data = entry_policy_environment(
            root, env, server, "entry-" + name, "automatic"
        )
        before = request_count(server)
        terminal = PtyProcess([binary, prompt], root, policy_env)
        try:
            if expect_repl:
                output = terminal.expect("ask> ")
                if expect_error:
                    assert b"judge failed; continuing conversation" in output, output
                terminal.send(b"!q\n")
                assert terminal.process.wait(timeout=5) == 0
            else:
                terminal.expect("echo: " + prompt)
                assert terminal.process.wait(timeout=5) == 0
                assert (policy_data / "quick-resume.json").exists()
            requests = request_slice(server, before)
            assert len(requests) == 2, requests
            assert_judge_request(requests[1], prompt)
        finally:
            stop_pty(terminal)

    priority_env, _ = entry_policy_environment(
        root, env, server, "entry-priority", "automatic"
    )
    for arguments in (
        ["--no-repl", "judge exit no repl"],
        ["--json", "judge exit json"],
    ):
        before = request_count(server)
        terminal = PtyProcess([binary, *arguments], root, priority_env)
        try:
            assert terminal.process.wait(timeout=8) == 0
            assert len(request_slice(server, before)) == 1, request_slice(server, before)
        finally:
            stop_pty(terminal)

    before = request_count(server)
    interactive = PtyProcess([binary, "-i", "judge exit interactive"], root, priority_env)
    try:
        interactive.expect("ask> ")
        assert len(request_slice(server, before)) == 1, request_slice(server, before)
        interactive.send(b"!q\n")
        assert interactive.process.wait(timeout=5) == 0
    finally:
        stop_pty(interactive)

    before = request_count(server)
    piped = subprocess.run(
        [binary, "judge exit piped"], cwd=root, env=priority_env,
        input="pipe body\n", text=True, capture_output=True, check=True
    )
    assert piped.stdout == "echo: judge exit piped\n\npipe body\n", piped
    assert len(request_slice(server, before)) == 1, request_slice(server, before)

    do_env, _ = entry_policy_environment(
        root, env, server, "entry-do", "always_exit"
    )
    terminal = PtyProcess([binary, "--do", "do quick resume"], root, do_env)
    try:
        terminal.expect("tool complete")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    resumed = PtyProcess([binary], root, do_env)
    try:
        resumed.expect("ask: resumed ")
        resumed.expect("[do]")
        resumed.expect("do> ")
        resumed.send(b"!q\n")
        assert resumed.process.wait(timeout=5) == 0
    finally:
        stop_pty(resumed)

    expired_env, expired_data = entry_policy_environment(
        root, env, server, "entry-expired", "always_exit"
    )
    terminal = PtyProcess([binary, "expire this"], root, expired_env)
    try:
        terminal.expect("echo: expire this")
        assert terminal.process.wait(timeout=5) == 0
    finally:
        stop_pty(terminal)
    quick_path = expired_data / "quick-resume.json"
    quick = json.loads(quick_path.read_text())
    quick["marked_at"] = int(time.time()) - 30
    quick_path.write_text(json.dumps(quick))
    expired = PtyProcess([binary], root, expired_env)
    try:
        fresh = expired.expect("ask> ")
        assert b"resumed" not in fresh, fresh
        expired.send(b"!q\n")
        assert expired.process.wait(timeout=5) == 0
    finally:
        stop_pty(expired)

    explicit_env, explicit_data = entry_policy_environment(
        root, env, server, "entry-explicit", "always_exit"
    )
    terminal = PtyProcess([binary, "old request"], root, explicit_env)
    try:
        terminal.expect("echo: old request")
        assert terminal.process.wait(timeout=5) == 0
        assert (explicit_data / "quick-resume.json").exists()
    finally:
        stop_pty(terminal)
    replacement = PtyProcess([binary, "new request"], root, explicit_env)
    try:
        output = replacement.expect("echo: new request")
        assert b"resumed" not in output, output
        assert replacement.process.wait(timeout=5) == 0
        quick = json.loads((explicit_data / "quick-resume.json").read_text())
        messages = quick["session"]["messages"]
        assert any(m.get("content") == "new request" for m in messages), messages
        assert all(m.get("content") != "old request" for m in messages), messages
    finally:
        stop_pty(replacement)


def run_protocol_call(binary, server, root, env, name, config, *arguments):
    config_home = root / (name + "-config")
    config_home.mkdir()
    (config_home / "config.json").write_text(json.dumps(config))
    call_env = env.copy()
    call_env["ASK_CONFIG_HOME"] = str(config_home)
    call_env["ASK_DATA_HOME"] = str(root / (name + "-data"))
    with server.requests_lock:
        before = len(server.requests)
    result = subprocess.run(
        [binary, "--no-repl", *arguments], cwd=root, env=call_env,
        stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
    )
    with server.requests_lock:
        received = server.requests[before:]
    assert received, (name, result)
    return result, received


def exercise_generation_settings(binary, server, root, env):
    base = f"http://127.0.0.1:{server.server_port}"

    openai = protocol_config("openai", base + "/v1", "mock-model")
    openai["settings"].update({
        "temperature": 0.2,
        "top_p": 0.8,
        "reasoning_effort": "auto",
        "thinking_budget_tokens": 0,
        "stream_output": True,
        "custom_parameters": {
            "temperature": 0.35,
            "response_format": {"type": "json_object"},
            "model": "blocked-model",
            "messages": [{"role": "user", "content": "blocked"}],
            "contents": [{"role": "user", "parts": [{"text": "blocked"}]}],
            "system": "blocked system",
            "systemInstruction": {"parts": [{"text": "blocked"}]},
            "tools": [{"blocked": True}],
            "tool_choice": "required",
            "stream": True,
        },
    })
    _, requests = run_protocol_call(
        binary, server, root, env, "openai-generation", openai,
        "--no-stream", "openai settings"
    )
    path, body = requests[-1]
    assert path == "/v1/chat/completions", (path, body)
    assert body["stream"] is False and body["model"] == "mock-model", body
    assert body["messages"][-1]["content"] == "openai settings", body
    assert_permission_context(requests[-1], "ASK_READ_ONLY")
    for protected in ("contents", "system", "systemInstruction"):
        assert protected not in body, (protected, body)
    assert_readonly_tools(requests[-1])
    assert body["tool_choice"] == "auto", body
    assert all(tool.get("blocked") is not True for tool in body["tools"]), body
    assert body["temperature"] == 0.35 and body["top_p"] == 0.8, body
    assert body["reasoning_effort"] == "medium", body
    assert body["response_format"] == {"type": "json_object"}, body

    openai["settings"]["custom_parameters"] = {}
    _, requests = run_protocol_call(
        binary, server, root, env, "openai-stream", openai, "openai stream"
    )
    assert requests[-1][1]["stream"] is True, requests[-1]
    assert requests[-1][1]["temperature"] == 0.2, requests[-1]

    limited = protocol_config("openai", base + "/v1", "limited-model")
    limited["providers"][0]["model_capabilities"] = {
        "limited-model": {
            "tools": False, "streaming": False, "thinking": False,
            "temperature": False, "top_p": False, "json": False,
            "context_window": 8192,
        }
    }
    limited["settings"].update({
        "temperature": 0.2, "top_p": 0.8, "reasoning_effort": "high",
        "stream_output": True,
        "custom_parameters": {"temperature": 0.9, "top_p": 0.1,
                               "response_format": {"type": "json_object"}},
    })
    _, requests = run_protocol_call(
        binary, server, root, env, "model-capabilities", limited, "limited capabilities"
    )
    body = requests[-1][1]
    assert body["stream"] is False, body
    assert "tools" not in body and "tool_choice" not in body, body
    assert "temperature" not in body and "top_p" not in body, body
    assert "reasoning_effort" not in body and "response_format" not in body, body

    openrouter = protocol_config("openai", base + "/v1", "router-model")
    openrouter["default_provider"] = "openrouter"
    openrouter["providers"][0].update({
        "id": "openrouter", "name": "OpenRouter", "protocol": "openai"
    })
    openrouter["settings"].update({
        "reasoning_effort": "high", "stream_output": False,
        "custom_parameters": {},
    })
    _, requests = run_protocol_call(
        binary, server, root, env, "openrouter-generation", openrouter, "router settings"
    )
    body = requests[-1][1]
    assert body["stream"] is False, body
    assert body["reasoning"] == {"effort": "high"}, body
    assert "reasoning_effort" not in body, body

    anthropic = protocol_config("anthropic", base, "claude-mock")
    anthropic["settings"].update({
        "max_output_tokens": 4096,
        "temperature": 0.3,
        "top_p": 0.7,
        "reasoning_effort": "high",
        "thinking_budget_tokens": 1200,
        "stream_output": False,
        "custom_parameters": {
            "temperature": 0.95, "top_p": 0.1, "max_tokens": 1300,
        },
    })
    _, requests = run_protocol_call(
        binary, server, root, env, "anthropic-generation", anthropic, "anthropic settings"
    )
    body = requests[-1][1]
    assert body["thinking"] == {"type": "enabled", "budget_tokens": 1200}, body
    assert "temperature" not in body and "top_p" not in body, body
    assert body["max_tokens"] == 1300, body
    assert_permission_context(requests[-1], "ASK_READ_ONLY")

    anthropic["settings"]["stream_output"] = True
    anthropic["settings"]["custom_parameters"] = {}
    _, requests = run_protocol_call(
        binary, server, root, env, "anthropic-stream-generation",
        anthropic, "anthropic stream settings"
    )
    body = requests[-1][1]
    assert body["stream"] is True, body
    assert body["thinking"] == {"type": "enabled", "budget_tokens": 1200}, body
    assert "temperature" not in body and "top_p" not in body, body
    assert_permission_context(requests[-1], "ASK_READ_ONLY")

    gemini = protocol_config("gemini", base + "/v1beta", "gemini-2.5-flash")
    gemini["settings"].update({
        "max_output_tokens": 2048,
        "temperature": 0.4,
        "top_p": 0.9,
        "reasoning_effort": "auto",
        "thinking_budget_tokens": 0,
        "stream_output": True,
        "custom_parameters": {},
    })
    _, requests = run_protocol_call(
        binary, server, root, env, "gemini-generation", gemini, "gemini settings"
    )
    path, body = requests[-1]
    assert path.endswith(":streamGenerateContent?alt=sse"), path
    generation = body["generationConfig"]
    assert generation["maxOutputTokens"] == 2048, generation
    assert generation["temperature"] == 0.4 and generation["topP"] == 0.9, generation
    assert generation["thinkingConfig"] == {
        "includeThoughts": False, "thinkingBudget": -1
    }, generation
    assert_permission_context(requests[-1], "ASK_READ_ONLY")

    gemini["settings"]["stream_output"] = False
    _, requests = run_protocol_call(
        binary, server, root, env, "gemini-complete-generation",
        gemini, "gemini complete settings"
    )
    path, body = requests[-1]
    assert path.endswith(":generateContent"), path
    generation = body["generationConfig"]
    assert generation["temperature"] == 0.4 and generation["topP"] == 0.9, generation
    assert generation["thinkingConfig"]["thinkingBudget"] == -1, generation
    assert_permission_context(requests[-1], "ASK_READ_ONLY")


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
    compact_config["providers"][0]["context_window"] = 8192
    compact_config["settings"]["max_output_tokens"] = 256
    (compact_home / "config.json").write_text(json.dumps(compact_config))
    compact_env = env.copy()
    compact_env["ASK_CONFIG_HOME"] = str(compact_home)
    compact_env["ASK_DATA_HOME"] = str(root / "compact-data")
    terminal = PtyProcess([binary, "x" * 10000], root, compact_env)
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
    server.requests = []
    server.requests_lock = threading.Lock()
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
                "default_model": "stale-top-level-model",
                "providers": [{
                    "id": "mock",
                    "name": "Mock",
                    "protocol": "openai",
                    "base_url": f"http://127.0.0.1:{server.server_port}/v1",
                    "api_key": "",
                    "api_key_env": "",
                    "models": ["mock-model", "mock-model-2"],
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
                    "custom_parameters": {"seed": 7},
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

            with server.requests_lock:
                before_json = len(server.requests)
            structured = subprocess.run(
                [binary, "--no-repl", "--json", "json test"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            payload = json.loads(structured.stdout)
            assert payload["text"] == "echo: json test"
            assert payload["model"] == "mock-model", payload
            assert payload["usage"]["total_tokens"] == 12
            with server.requests_lock:
                json_requests = server.requests[before_json:]
            assert json_requests and json_requests[-1][1]["stream"] is False, json_requests

            agent = subprocess.run(
                [binary, "--no-repl", "--do", "make a file"], cwd=root, env=env,
                stdin=subprocess.DEVNULL, text=True, capture_output=True, check=True
            )
            assert agent.stdout == "tool complete\n", agent
            assert (root / "made-by-tool.txt").read_text() == "tool worked"
            assert (data_home / "sessions.db").exists()
            exercise_repl(binary, root, env)
            exercise_permissions(binary, server, root, env)
            exercise_entry_policy(binary, server, root, env)
            exercise_protocols_and_auto_compact(binary, server, root, env)
            exercise_generation_settings(binary, server, root, env)
    finally:
        server.shutdown()
        server.server_close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("binary")
    args = parser.parse_args()
    run(os.path.abspath(args.binary))
