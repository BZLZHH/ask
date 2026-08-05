# ask Quick Start

`ask` is a local command-line AI client for three kinds of work: quick Q&A, terminal conversations with persistent context, and multi-role AI Conference. It supports OpenAI-compatible APIs, Anthropic, and Gemini.

## 1. Build

Requires C++20, CMake, libcurl, JsonCpp, SQLite, ncursesw, libedit; on Linux, Bubblewrap is needed for model-executed commands.

Debian or Ubuntu:

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
  libcurl4-openssl-dev libjsoncpp-dev libsqlite3-dev libncursesw5-dev libedit-dev bubblewrap
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The examples below use `./build/ask` from the repository root. After installation, use `ask` directly.

## 2. Configure a Provider

Open the configuration interface:

```sh
./build/ask --config
```

Under `Providers`, enable or add a provider, fill in the protocol, API URL, default model, and API key. Storing the key in an environment variable is recommended over writing it into the config file. For example, with the built-in DeepSeek provider:

```sh
export DEEPSEEK_API_KEY='your-api-key'
```

When done, select a default provider and model under General Settings. Navigate with arrow keys, Enter to edit or confirm, Esc to go back, then choose `Save changes`.

## 3. First Conversation

Run in a project directory:

```sh
./build/ask "Explain the build process for this project"
```

Specify a model for this session:

```sh
./build/ask --provider deepseek --model deepseek-v4-flash "Review the CMake configuration in the current directory"
```

In interactive mode, whether the REPL starts after the first answer is controlled by `Conversation entry`. Once in the REPL, type questions directly; Ctrl-C cancels the in-progress request but keeps the session.

Common REPL commands:

```text
!help                 Show command help
!config               Open config and return to current session
!do <task>            Enable full tools for the next round only
!ask <task>           Force read-only capability for the next round
!model                Switch provider and model for this session
!compact              Compact older active context
?COMMAND              Run your own shell command and add output as context
!q                    Save and quit
```

Sessions persist. Resume the most recent:

```sh
./build/ask resume
```

Or run bare `./build/ask` within 10 seconds of an auto-exit in the same directory for quick recovery.

## 4. Ask Mode and Do Mode

Default Ask mode only provides read-only capabilities: reading files, searching text, viewing restricted Git information and system status. The model cannot modify the workspace.

When the task genuinely needs file changes, command execution, or network access, start Do mode:

```sh
./build/ask --do "Investigate the test failure, fix it, and run the relevant tests"
```

Do mode is still bounded: files must be under the current workspace, model commands run inside a Linux Bubblewrap sandbox, and privilege escalation always requires per-use user confirmation. The model can also request one-time or session-scoped Do authorization from Ask mode with a stated reason; decline when unsure.

## 5. Scripts and Pipelines

Non-TTY contexts default to single execution and exit, ideal for pipelines:

```sh
git diff | ./build/ask "Review this diff and flag high-risk issues"
./build/ask --no-repl --no-stream "Generate a concise commit message"
```

For machine-readable output, use JSON:

```sh
result=$(printf '%s' 'Explain this log' | ./build/ask --json)
printf '%s\n' "$result" | jq -r '.text'
```

`--json` disables streaming; stdout receives a single JSON object; tool progress and diagnostics go to stderr.

## 6. AI Conference

When the task needs solution comparison, risk review, or structured discussion, use standalone conference mode:

```sh
./build/ask conference "Create an executable security release plan for this project within two weeks, with rollback paths"
```

A new conference first enters `Preparing plan`. The moderator generates a reviewable meeting plan via a real model call, including each seat's name, role, responsibility, provider, and model. The user reviews and edits in `Review meeting plan` before discussion begins.

The conference UI uses three panes: agenda & status, discussion timeline, and control menu. Arrow keys and Enter handle common actions; the bottom input bar is for interjections, answering moderator questions, and internal REPL.

Conference REPL commands:

```text
/help                 Show conference REPL help
/status               View full status summary
/agenda               View agenda and phase conclusions
/members              View seats, responsibilities, and models
/questions            View moderator questions, options, and answers
/advance or /run      Advance to the next scheduled member
/pause /resume        Pause or resume
/answer <content>     Answer a moderator question
/next                 Pick the next speaker manually
/setup                Modify seats, models, rules, or discussion depth
/autopilot            Configure auto-advance and tool pre-authorization
/summary              View meeting summary
/export report.md     Export Markdown summary into the workspace
/end                  Stop the conference and preserve history
```

The moderator can ask subjective, objective, or mixed questions with a timeout. Objective questions accept arrow-key selection; free-form input works for all types. Advisor seats cannot address the user directly — they suggest the moderator ask.

Resume a saved conference:

```sh
./build/ask conference resume
```

Full conference operations are documented in [AI_CONFERENCE_GUIDE.md](AI_CONFERENCE_GUIDE.md); product design and boundaries are in [AI_CONFERENCE.md](AI_CONFERENCE.md).

## 7. FAQ

**"Provider unavailable" or authentication failure**

Run `./build/ask --config`, verify the provider is enabled, the API URL and default model are correct. If using environment variables, confirm the current shell has exported the relevant key.

**Model doesn't use tools or streaming**

Check the current model's capability profile in configuration. Parameters unsupported by the model or protocol are automatically omitted.

**Model commands don't execute**

Confirm `--do` is used or Do mode has been authorized; on Linux, also install Bubblewrap. Without a security sandbox, commands are refused rather than falling back to bare host execution.

**Conference won't auto-continue**

Check conference status: `Awaiting user` means the moderator is waiting for an answer or decision; `Paused` needs `/resume`; for auto-advance, use `/auto run`. Timeline entries for `Error`, `Output limit`, and tool events explain the specific cause.

**Where is data stored?**

Configuration defaults to `$XDG_CONFIG_HOME/ask/config.json` or `~/.config/ask/config.json`; sessions, conference records, and local state live under the corresponding XDG data directory. All conferences are browsable and resumable via `ask conference resume`.
