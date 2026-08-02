# Changes

This file records user-visible changes to `ask`.

## 0.6.0 - 2026-08-02

### Added

- Added a small system prompt template layer with `{{variable}}` substitution, `{{#if}}`/`{{#unless}}` conditionals, provider/protocol/model keyed conditions, and template escaping.
- Added a Unicode-aware token estimator calibrated by model family and protocol-specific message overhead for more accurate compaction decisions.
- Added a structured compaction prompt with session metadata, tool context, previous-summary merge instructions, and explicit memory sections.
- Added explicit agent tool-loop instructions and a global untrusted-data rule to the runtime permission block.
- Added warnings for malformed or unknown prompt template syntax.
- Added a fixed core instruction block at the front of every model system prompt with grounded-answer, tool-use, error-recovery, and output-format guidance.
- Added Anthropic prompt-caching `cache_control` breakpoints on the system prompt, first user message, and final message.
- Added provider cache metric parsing for OpenAI cached tokens, Anthropic cache read/creation tokens, and Gemini cached content tokens.
- Added a `!cache` REPL command and JSON usage fields that report provider cache utilization for the last request.
- Added cumulative conversation cache statistics that persist with the session and are shown by `!cache`.
- Added a local cacheable-prefix estimate fallback when a provider does not report cache metrics.

### Changed

- System prompt templates are expanded before the runtime permission block is appended; existing plain-text prompts remain unchanged.
- Compaction transcripts now distinguish user messages, assistant tool calls, and tool results instead of flattening every message into the same role line.
- The Judge prompt now receives conversation metadata and an explicit conservative decision rubric, so it exits only when the exchange is confidently complete and otherwise prefers continuing.
- Runtime permission tool lists are now derived from tool schemas instead of hardcoded names.
- The Judge prompt explicitly rejects instructions found inside the quoted user prompt or assistant answer.
- Gemini tool results resolve the function name from the matched tool call instead of assuming the call id is the function name.
- Tool-round-limit responses are normalized into a final assistant message so no dangling tool call remains in history.
- Historical compaction summaries are now injected as a single trusted context message instead of a fake user/assistant acknowledgement.
- Tool schema descriptions now include decision guidance for when to use read-only, mutation, web, and permission-request tools.
- Time template variables now use the session start time so the configured system prompt remains stable and cache-friendly.
- The fixed agent-loop and untrusted-data rules moved into the stable core instruction prefix.
- The runtime permission text stays stable when a request reaches the tool-round limit, avoiding an unnecessary cache miss on the final request.
- Compaction summaries are appended up to a token cap, then replaced, so the cached prefix remains reusable without allowing the summary to grow without bound.
- Gemini cache metric parsing now falls back across common usage field names, and compaction explicitly asks the model not to repeat the previous summary verbatim.
- The runtime permission block now travels as the final harness-generated message instead of living inside the system prompt, so permission state changes no longer invalidate the stable prompt and history cache prefix.
- The Judge prompt is simpler and more explicit, and decision parsing now accepts common Chinese, bye/quit aliases, and explanatory phrases that still identify the final CONTINUE or EXIT decision.
- Conference prompts now include global untrusted-data rules, split meeting context into stable and per-turn messages for cache reuse, use token-based history compaction, and place moderator output contracts in a dedicated final section.

## 0.5.0 - 2026-07-25

### Added

- Added native read-only `git_status`, `git_diff`, `git_log`, and `git_show` tools with structured branch, file, and conflict state.
- Added a model capability registry with protocol defaults and per-model overrides for tools, streaming, Thinking, sampling, JSON mode, and context windows.

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
