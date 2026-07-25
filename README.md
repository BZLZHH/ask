# ask

A Unix and Unix-like command-line client for quick AI questions, persistent conversations, and explicitly authorized local automation.


Current version: `0.3.0`

## Features

- Ask directly from the command line with `ask "..."`.
- Stream responses from OpenAI-compatible, Anthropic, and Gemini APIs.
- Configure providers, models, endpoints, credentials, headers, and context limits in a full-screen TUI.
- Resume persistent conversations through a session picker with message previews.
- Continue every terminal conversation in a libedit-powered REPL.
- Search history, complete commands, enter multiline prompts, and cancel input or generation with Ctrl-C.
- Enable local tools with `--do` for files, commands, HTTP requests, web pages, and web search.
- Run multi-round `model -> tool -> model` agent loops.
- Isolate model-generated commands with Bubblewrap on Linux.
- Approve one exact command to run outside the workspace sandbox as the current user.
- Automatically compact active context at 70% of the configured context window while retaining the original transcript.
- Use stdin, stdout redirection, pipelines, and structured JSON output in scripts.

## Provider Protocols

| Protocol | Configuration value | Non-streaming | Streaming | Tool calls |
|---|---|---:|---:|---:|
| OpenAI Chat Completions | `openai` or `openai_chat` | Yes | Yes | Yes |
| Anthropic Messages | `anthropic` | Yes | Yes | Yes |
| Google Gemini generateContent | `gemini` | Yes | Yes | Yes |

The OpenAI-compatible adapter works with OpenAI, DeepSeek, OpenRouter, Ollama, and other services that expose a compatible `/chat/completions` endpoint.

## Build

`ask` requires a C++20 compiler and these system libraries:

- libcurl
- JsonCpp
- SQLite 3
- ncursesw
- libedit
- Bubblewrap, required for sandboxed model commands on Linux

Fedora:

```sh
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config \
  libcurl-devel jsoncpp-devel sqlite-devel ncurses-devel libedit-devel bubblewrap
```

Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
  libcurl4-openssl-dev libjsoncpp-dev libsqlite3-dev libncursesw5-dev libedit-dev bubblewrap
```

Arch Linux:

```sh
sudo pacman -S base-devel cmake ninja pkgconf curl jsoncpp sqlite ncurses libedit bubblewrap
```

Configure, build, and test:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Install the executable, README, and license:

```sh
sudo cmake --install build
```

You can also run `./build/ask` directly.

The chat client can be built on Unix-like systems with the required dependencies. The model-command sandbox currently uses Linux Bubblewrap. If no secure sandbox backend is available, model-generated commands fail closed instead of silently running without isolation.

## First-Time Configuration

Open the configuration TUI:

```sh
ask --config
```

The configuration interface supports:

- A General Settings home page for session behavior, the default model, AI calls, and providers
- Breadcrumb headings that preserve the full Settings / Provider / Model hierarchy
- Nested Providers and Models pages for connection-specific configuration
- Arrow-key navigation, Enter to open or select, and Esc to return
- Transactional editing with explicit Save changes and Cancel actions
- Provider creation, field-by-field editing, deletion, enablement, and model discovery
- OpenAI-compatible, Anthropic, and Gemini protocols
- API base URLs
- API keys stored directly or read from environment variables
- Additional HTTP headers
- Manual model lists and model discovery
- Per-provider context windows and timeouts
- A nested AI call page for Thinking strength and budget, Temperature, Top P, maximum output tokens, streaming, and advanced request JSON
- Provider-default sampling and Thinking values that omit unsupported overrides instead of forcing them on every model
- Maximum tool-loop rounds
- Automatic compaction ratio and system prompt

The `ask settings ➔ AI call` page uses the same navigation model as the rest of the TUI: Up/Down moves focus, Left/Right adjusts a value, Enter opens a selector or editor, and Esc returns to General. Temperature and Top P accept explicit values from `0.0` to `1.0`; choosing `Provider default` leaves the field out of the request. A Thinking budget of `0` means automatic budgeting from the selected strength.

Thinking settings are translated for each protocol rather than copied verbatim. OpenAI-compatible providers receive `reasoning_effort`; OpenRouter receives its `reasoning` object; Anthropic receives `thinking.type` and `budget_tokens`; Gemini receives `generationConfig.thinkingConfig`. Anthropic Thinking removes conflicting Temperature and Top P values and requires its budget to remain below `max_tokens`. Provider and model support still varies, so `Provider default` is the safest compatibility setting.

Advanced request JSON must be an object. It is recursively merged after the normal generation controls, so it can override ordinary provider-specific parameters. Request structure remains protected: it cannot replace `model`, messages or contents, system instructions, tools, tool choice, or the streaming flag. Protocol safety checks still run on the final Anthropic Thinking request.

Environment variables are recommended for API keys. For example, the built-in DeepSeek provider reads `DEEPSEEK_API_KEY`:

```sh
export DEEPSEEK_API_KEY='your-api-key'
ask --provider deepseek --model deepseek-v4-flash "Hello"
```

An environment variable value takes precedence over a key stored in the configuration file.

## Command-Line Usage

```text
Usage:
  ask [options] [prompt ...]
  ask --do [options] [prompt ...]
  ask --config
  ask resume [session-id] [--provider ID] [--model MODEL]

Options:
  -p, --provider ID    Override the provider for this session
  -m, --model MODEL    Override the model for this session
      --do             Start with workspace tools enabled
  -i, --interactive    Enter the REPL even when input was piped
      --no-repl        Exit after one response
      --no-stream      Wait for the complete response before printing
      --json           Emit one JSON object and disable streaming
  -q, --quiet          Hide tool progress messages
      --config         Open the settings TUI
  -h, --help           Show help
      --version        Show the version
```

Examples:

```sh
# Use the configured default provider and model
ask "Explain edge-triggered epoll"

# Override the provider and model for this session
ask --provider deepseek --model deepseek-v4-flash "Review this SQL query"

# Allow the model to use tools inside the current directory
ask --do "Inspect this project and fix its build"

# Resume a saved conversation
ask resume
ask resume 20260719-120000-abcdef

# Run one non-streaming request and exit
ask --no-repl --no-stream "Reply with only yes or no"
```

## Streaming

Responses stream to stdout by default when `Stream output` is enabled under `ask settings ➔ AI call`. OpenAI-compatible SSE deltas, Anthropic content blocks, and Gemini `streamGenerateContent` events are decoded incrementally. Tool names and JSON arguments are assembled internally and are not printed as protocol data. Later model turns in a do-mode tool loop continue streaming normally.

```sh
ask "Write a four-line poem"
```

Use `--no-stream` to wait for the complete response regardless of the saved setting:

```sh
ask --no-stream "Summarize this error"
```

Pressing Ctrl-C during generation cancels the current HTTP stream and returns to the REPL without exiting the conversation. Both `--no-stream` and `--json` override the saved streaming preference; `--json` always disables streaming so stdout remains one complete, valid JSON object.

## REPL

When stdin and stdout are both terminals, `ask` enters the REPL after the first response. Regular input continues the conversation. The following commands are also available:

| Input | Behavior |
|---|---|
| `?COMMAND` | Run a user shell command and retain its output, errors, and exit code as conversation context |
| `!do PROMPT` | Enable tools for the next turn only |
| `!ask PROMPT` | Disable all tools for the next turn only |
| `!model` | Switch the provider and model for the current session |
| `!config` | Open configuration and then return to the current conversation |
| `!compact` | Ask the current model to summarize older active context |
| `!help` | Show REPL commands |
| `!q` | Save and quit |

Editing behavior:

- End a line with `\` to continue the prompt on another line.
- Press Ctrl-R to search persistent history.
- Press Tab to complete special commands and use normal filename completion.
- Press Ctrl-C while editing to clear the current input.
- Press Ctrl-D on an empty input to save and exit.
- Prefix `!` or `?` with a backslash to enter it literally, for example `\!q`.

`!do` and `!ask` are one-turn overrides. They do not permanently change the session's base mode.

## Pipelines and Scripts

If stdin or stdout is not a TTY, `ask` runs once and exits. It does not open a TUI or wait for another prompt.

```sh
# Combine an instruction with piped input
git diff | ask "Review this patch"

# Save plain text output
ask --no-repl "Generate a commit message" > commit-message.txt

# Read structured output
result=$(printf '%s' 'Explain this log' | ask --json)
printf '%s\n' "$result" | jq -r '.text'
```

When both an argv prompt and piped stdin are present, `ask` combines them as `prompt + blank line + stdin`. Diagnostics and tool progress go to stderr. Plain answers or JSON go to stdout.

JSON output has this shape:

```json
{
  "session": "20260719-120000-abcdef",
  "provider": "deepseek",
  "model": "deepseek-v4-flash",
  "text": "...",
  "finish_reason": "stop",
  "usage": {
    "prompt_tokens": 42,
    "completion_tokens": 18,
    "total_tokens": 60
  }
}
```

## Do Mode and Security

Do mode exposes these tools to the model:

| Tool | Purpose |
|---|---|
| `read_file` | Read part of a regular file inside the workspace |
| `write_file` | Write or append a file inside the workspace |
| `list_files` | List workspace files and directories |
| `run_command` | Run a shell command in the Bubblewrap sandbox |
| `fetch_http` | Fetch a public HTTP resource with SSRF, timeout, and size protections |
| `browse_page` | Fetch a public web page and convert it to readable text |
| `web_search` | Search the public web |

Model-provided file paths are resolved against the directory where `ask` started. Absolute external paths, `..` traversal, and symbolic links escaping the workspace are rejected.

Sandboxed model commands have these boundaries:

- Host environment variables are cleared to protect API keys and other secrets.
- System runtime files are mounted read-only.
- `/tmp` is private.
- The startup directory is the only host-writable mount.
- Commands keep the current user ID and never use root or sudo.
- Failure to start Bubblewrap causes the command to be rejected.

A model can request `elevated: true` for one command. Here, "elevated" only means running outside the workspace sandbox; the command still runs as the current unprivileged user. The approval prompt shows the reason, exact command, working directory, and environment policy. Approval applies once, is never cached, and is denied automatically without a controlling terminal.

A command entered directly by the user with `?COMMAND` is not a model tool call and runs in the user's normal shell environment.

Network tools accept only `http` and `https`. They reject loopback, private, link-local, multicast, and `.local` destinations, pin validated DNS results, and validate every redirect target again.

## Sessions and Context

Sessions, tool calls, and complete message history are stored in SQLite. `ask resume` shows timestamps, titles, modes, providers, models, and recent message previews. Resuming never repeats the last request automatically.

Before every request, `ask` estimates the size of:

- The system prompt
- Tool schemas
- The current compact summary
- Active messages
- Pending user input
- Reserved output tokens

When the prediction reaches the configured share of the model context window, `ask` asks the active model to create a compact summary. The default threshold is 70%. The original transcript remains in SQLite; only the active API request view advances to the summary and recent turns.

## File Locations

| Data | Explicit location | Default location |
|---|---|---|
| Configuration | `$ASK_CONFIG_HOME/config.json` | `$XDG_CONFIG_HOME/ask/config.json` or `~/.config/ask/config.json` |
| Session database | `$ASK_DATA_HOME/sessions.db` | `$XDG_DATA_HOME/ask/sessions.db` or `~/.local/share/ask/sessions.db` |
| REPL history | `$ASK_DATA_HOME/history` | `$XDG_DATA_HOME/ask/history` or `~/.local/share/ask/history` |

Directories are created with mode `0700`. Configuration, database, and history files use mode `0600`.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The test suite covers configuration and session persistence, AI call validation and protocol request mapping, CLI parsing, path traversal, Bubblewrap isolation, approval boundaries, private-network rejection, OpenAI/Anthropic/Gemini streaming, fragmented tool calls, JSON and pipeline behavior, Ctrl-C cancellation, and fixed-size settings/model/resume TUI flows in a PTY.

## License

This project is released under the [MIT License](LICENSE).

