# Changes

This file records user-visible changes to `ask`.

## 0.4.0 - 2026-07-25

### Added

- Added a GitHub Actions Linux build matrix for Debug and Release artifacts with SHA-256 checksums.
- Added read-only tools to ordinary ask mode for workspace reads, directory listing, literal text search, and strictly allowlisted commands.
- Added constrained system-status access for NVIDIA GPU, kernel, CPU, memory, disk, and uptime information.
- Added a model-initiated do permission screen with Deny, Allow once, and Allow for conversation choices.
- Added conversation-scoped do upgrades that persist through save, explicit resume, and quick resume without changing global defaults.
- Added per-request model permission context that names the current access state, current tools, full do tools, approval choices, and authorization lifetime without polluting saved conversation history.
- Added configurable first-response behavior: Automatic, Always continue, and Always exit.
- Added an independent Judge provider/model that classifies whether an interactive conversation should continue, with safe fallback to the REPL on failure or invalid output.
- Added one-time 10-second quick resume for automatically exited conversations, including restored provider, model, history, working directory, and do mode.
- Added a hierarchical `ask settings ➔ Conversation entry` page using direction keys, Enter, and Esc.

### Changed

- Ordinary sessions now display `[ask/read-only]`; `!ask` forces a read-only turn and disables permission requests, while `!do` remains a full-tool one-turn override.
- Read-only commands use fixed executable paths, direct argument vectors, per-command option validation, and a read-only workspace mount instead of model-controlled shell syntax.
- `--json` now remains one-shot even when run directly from a terminal.
- `--no-repl`, `--json`, piped/non-TTY calls, and explicit `-i` bypass automatic Judge evaluation.

### Fixed

- Permission requests now default to Deny and are rejected automatically when no interactive terminal is available.
- One-time do access is consumed after exactly the next model response/tool batch and is never saved.
- Models now receive explicit `DO_ONCE_THIS_RESPONSE` and post-consumption `ASK_READ_ONLY` states instead of having to infer authorization lifetime from changing tool schemas.
- Judge calls now use provider-compatible minimal Thinking controls, reserve enough output tokens for a final decision, and accept unambiguous quoted or Markdown-wrapped `CONTINUE`/`EXIT` responses.

## 0.3.0 - 2026-07-25

### Added

- Added a hierarchical Settings TUI with General, Advanced, Providers, and Models pages.
- Added breadcrumb headings such as `ask settings ➔ AI call`, transactional Save/Cancel behavior, and direction-key navigation.
- Added AI call settings for Temperature, Top P, Thinking strength, Thinking token budget, maximum output tokens, default streaming behavior, and advanced request JSON.
- Added protocol-specific generation mapping for OpenAI-compatible APIs, OpenRouter, Anthropic, and Gemini.
- Added recursive advanced-parameter merging while protecting request structure fields such as the model, messages, tools, system instructions, and streaming mode.
- Added configuration compatibility, request-body, streaming-priority, and fixed `80×24` Settings TUI tests.

### Changed

- Provider-default sampling and Thinking settings now omit request overrides until the user explicitly enables them.
- Anthropic Thinking now removes conflicting sampling parameters and validates its token budget after advanced parameters are merged.
- The saved `Stream output` setting controls normal requests; `--no-stream` and `--json` continue to force non-streaming output.
- The configured provider's `default_model` is now the authoritative model for new sessions.
- The built-in DeepSeek models are now `deepseek-v4-flash` and `deepseek-v4-pro`, with `deepseek-v4-flash` as the default.

### Fixed

- Fixed stale top-level model values causing DeepSeek requests to use `deepseek-chat` despite a different provider default.
- Fixed unclear Settings hierarchy and provider configuration appearing as the root settings screen.
- Fixed advanced Anthropic parameters being able to reintroduce sampling fields that conflict with Thinking.

## 0.2.0

- Initial public version of the command-line client, persistent sessions, provider adapters, streaming, REPL, and workspace tools.
