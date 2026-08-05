# AI Conference Guide

AI Conference is a multi-role collaboration workflow independent of ordinary user-AI conversations. You provide a goal; a moderator, experts, auditors, and a recorder discuss, verify, review risks, and produce traceable conclusions around a shared topic.

This guide covers creation, operation, autopilot, tool authorization, and export. See [AI_CONFERENCE.md](AI_CONFERENCE.md) for the functional specification.

## 1. Prerequisites

Build the project and configure a model provider:

```sh
cmake -S . -B build-conference
cmake --build build-conference
./build-conference/ask --config
```

Enable at least one working model provider in the configuration UI and set a default model. AI Conference uses that provider for each seat's contributions and tool calls.

Start with goals that benefit from multi-perspective discussion, solution comparison, risk review, or phased execution:

```sh
./build-conference/ask conference "Create a secure release plan for this project, executable within two weeks, with rollback strategy"
```

Optionally specify provider and model:

```sh
./build-conference/ask conference "Review the cache refactoring proposal" --provider deepseek --model deepseek-v4-flash
```

## 2. Creating and Resuming Conferences

### Creating a Conference

Running `ask conference "goal"` creates an independent conference record. The initial conference includes:

- Moderator: clarifies the goal, maintains the agenda, organizes contributions, and drives convergence.
- Expert: proposes solutions, technical trade-offs, and actionable recommendations.
- Auditor: examines risks, counterexamples, missing assumptions, and rollback paths.
- Recorder: captures facts, candidate decisions, open questions, and action items.

The initial agenda typically covers goal constraints, candidate solutions, risk review, and action items. After creation, the conference enters `Preparing plan`: Moderator #0 calls a model to generate a reviewable proposal, specifically determining name, role, responsibility, provider, and model for seats #0 through #N. The user must confirm or adjust each seat in `Review meeting plan` before the conference can begin. The moderator only selects from currently enabled providers; unavailable providers fall back to the seat's original binding and are recorded in the timeline. All conference members and pre-session plans output plain text only; Markdown is forbidden.

The plan review page clearly presents:

- `#0` is the fixed moderator seat, handling only agenda coordination, opinion evaluation, disagreement resolution, and speaker assignment — it does not produce deep technical proposals on behalf of advisor seats.
- `#1` through `#6` are advisor seats; each can be individually configured with name, responsibility, provider, model, enabled status, and response cap.
- Discussion depth is `quick`, `standard`, `deep`, or `audit`, providing 4, 8, 16, or 24 rounds per agenda item as phase checkpoints. At each checkpoint, the moderator must evaluate contributions, record phase conclusions, and decide whether to dig deeper or advance to the next item; it never silently ends the conference.

Use arrow keys to select fields and Enter to modify on the review page. After confirming `Approve plan and start meeting`, Moderator #0 begins the first round. During a running conference, the same parameters can be adjusted via `Meeting parameters` or `/setup`; changes take effect from the next model request.

### Moderator Questions to the User

The moderator only asks the user when essential preferences, authorization, facts, or value judgments needed for progress are missing. Questions can be subjective, objective, or mixed; objective/mixed questions can include options, and all questions support a timeout from 30 seconds to 24 hours. When `Awaiting user`, the right control panel shows `Answer moderator question`; objective questions accept arrow-key and Enter selection, subjective questions accept free-text input. Answers can also be typed directly into the bottom input bar.

Advisor seats cannot address the user directly but may suggest the moderator ask a question. The moderator still decides whether it is necessary, formulates options, and sets the timeout. User answers, refusals, and timeouts are all written to the timeline. On timeout, the conference feeds "user did not answer within the time limit" to the moderator, requiring them to record the missing information, adopt conservative assumptions, or restructure the agenda. When autopilot is enabled, the process continues automatically from this point.

### Resuming a Conference

```sh
./build-conference/ask conference resume
```

Use arrow keys to select a saved conference and Enter to resume. Conference records are stored in the `conferences` subdirectory of the local data directory, containing state, agenda, timeline, tool events, and autopilot policy.

## 3. Main Interface and Status

On wide terminals, the main interface is split into three panes:

- Left pane `Agenda & State`: agenda, confirmed facts, and open questions.
- Center pane `Discussion`: member contributions, user interjections, system status, and tool results.
- Right pane `Controls`: frequent conference operations.
- Bottom input area: type free-form interjections or slash commands.

On narrow terminals, use `F1`, `F2`, `F3` to open discussion, agenda, and controls respectively.

Conference status meanings:

| Status | Meaning | Next Step |
| --- | --- | --- |
| `Awaiting plan approval` | Moderator has proposed seats, models, and depth; awaiting user review | Open `Review meeting plan`, approve or adjust |
| `Running` | Scheduling new AI contributions | Continue advancing, interject, or pause |
| `Paused` | No new contributions are scheduled | Resume, edit, or end |
| `Awaiting user` | Needs user input on an interjection, decision, or open question | Provide input to continue |
| `Completed` | Conference conclusions have been generated | Review or export summary |
| `Stopped` | User terminated | Review preserved history |

## 4. Keyboard Controls

The conference interface is keyboard-first; major operations are possible without memorizing commands.

| Key | Action |
| --- | --- |
| `Left` / `Right` | Switch focus between agenda, discussion, controls, and input |
| `Up` / `Down` | Select menu items, agenda items, or scroll discussion |
| `Enter` | Execute safe default action, confirm menu, or send input |
| `Space` | Pause or resume the conference |
| `Tab` / `Shift+Tab` | Forward/backward focus cycling |
| `Home` / `End` | Jump to start or end of discussion |
| `PageUp` / `PageDown` | Scroll discussion by page |
| `F1` / `F2` / `F3` | Focus discussion, agenda, or controls |
| `i` | Focus input bar |
| `?` | Open help |
| `Esc` | Cancel input, close overlay, or leave conference |
| `f` | Toggle live tracking in discussion; after browsing history, press `End` to rejoin |

When the input bar has focus, keys serve the internal REPL first: `Up` / `Down` browse recent input, `Tab` completes slash commands, `Ctrl-U` clears input. Regular characters (including UTF-8, spaces, and `?`) enter the input bar and do not trigger global pause or help shortcuts.

In the controls panel, use `Up` / `Down` to select operations, `Enter` to execute. Dangerous operations such as ending a conference or auto-authorizing tools show a confirmation menu with cancel as the default.

## 5. Basic Conference Workflow

A typical manually-driven workflow:

1. In the right control panel, select `Review meeting plan` and review the numbered seats, models, and discussion depth proposed by Moderator #0.
2. Select `Approve plan and start meeting`. The first speaker is Moderator #0.
3. Select `Advance assigned speaker` or enter `/advance`. The moderator evaluates existing contributions and assigns an advisor seat with `NEXT_SPEAKER`; after the advisor completes, control returns to the moderator for evaluation and reassignment.
4. The user can select `Choose next speaker` or enter `/next` to directly override the next assignment. Advisors can suggest the next speaker, but only moderator or user assignments take effect.
5. Read the center timeline and continue advancing members as needed; when scrolling up through history, auto-follow stops; press End to return to live.
6. Check the left pane for updated seats, facts, open questions, and candidate decisions.
7. When a user decision is needed, open the decision panel to confirm, reject, request evidence, or continue discussion.
8. Once discussion converges, select `Conclude meeting`, then review or export the summary.

Regular `/advance` rounds only provide read-only verification tools. This is suitable for viewing files, searching code, reading Git status, or running restricted read-only commands without modifying the workspace.

## 6. User Interjections, Goal Changes, and Decisions

User messages have the highest priority. Text entered in the input bar without a leading `/` is written directly to the timeline as a high-priority interjection and transitions a running conference to `Awaiting user`.

Example:

```text
The release must support zero-downtime; database migrations must not break backward compatibility.
```

Once the conference resumes, members prioritize handling this new constraint. Common commands:

| Command | Purpose |
| --- | --- |
| `/ask <role> <question>` | Pose a question to a specific role |
| `/answer <reply>` | Answer a pending moderator question; plain text also works |
| `/goal <new goal>` | Modify the conference goal; transitions to awaiting user |
| `/focus <agenda number>` | Switch current agenda item, e.g. `/focus 3` |
| `/status` | View current full status summary |
| `/agenda` | View agenda status and per-item phase conclusions |
| `/members` | View numbered seats, responsibilities, and model bindings |
| `/questions` | View moderator questions, options, status, and user answers |
| `/help` | Open internal REPL command help |
| `/run` / `/continue` | Shorthand for `/advance` / `/resume` |
| `/pause` / `/resume` | Pause or resume the conference |
| `/summary` | View current structured summary |
| `/setup` | Review or adjust conference depth, seats, models, and rules |
| `/next` | Select the next speaker with arrow keys and record user-stated reason |
| `/decision` | Open candidate decision panel |
| `/end` | Terminate conference and preserve history |

In the candidate decision panel, use arrow keys to choose:

- `Confirm decision`: Mark as user-confirmed.
- `Request more evidence`: Ask for additional evidence; stops autopilot.
- `Continue discussion`: Resume discussion without immediate conclusion.
- `Reject decision`: Record user rejection.

## 7. Tool Use and Single-Round Execution Authorization

Regular conference rounds default to read-only. Every tool request and result appears in the discussion timeline.

To grant one member write, command, or external query access for a single round only, select `Run user-approved execution` or enter:

```text
/execute
```

The system shows a confirmation prompt first. Once confirmed, the authorization is valid for exactly one round of the next member; it automatically reverts to read-only after that round.

This suits one-shot, well-scoped tasks like "modify one config file per the confirmed plan and run the designated test." For continuous automatic execution, use the autopilot feature in the next section and select only the needed tool permissions.

## 8. Moderator Intelligent Autopilot

Autopilot lets the moderator automatically schedule the next speaker, advance the agenda, use pre-authorized tools, and stop when a user decision is required. It is off by default and defaults to no write or external tool permissions.

### Configuring Autopilot

Select `Configure autopilot permissions` in the control panel, or enter:

```text
/autopilot
```

The configuration flow fully supports arrow keys:

1. Use `Up` / `Down` and Enter to choose manual mode or enable autopilot.
2. Select the maximum rounds per auto-run: 4, 8, 12, or 20.
3. In the permissions list, toggle individual tools with `Space` and save with Enter.
4. If non-read-only tools were selected, confirm the authorization dialog. Default focus is cancel.

Pre-authorizable tools:

| Tool | Capability | Autopilot Constraints |
| --- | --- | --- |
| `write_file` | Write workspace files | Limited to the conference workspace |
| `run_command` | Execute commands | Always runs in workspace sandbox; no auto-escalation |
| `fetch_http` | Request public HTTP/HTTPS resources | Subject to public-address and size limits |
| `browse_page` | Read public web page text | Subject to public-address and size limits |
| `web_search` | Public web search | Results recorded as auditable evidence |

With no tools checked, autopilot still runs but with read-only tools only. Even if the model requests an unchecked tool, the tool execution layer refuses the call.

### Running and Stopping

Once configured, select `Run moderator autopilot` in the control panel, or enter:

```text
/auto run
```

Quick commands:

```text
/auto on     # Enable autopilot with saved policy
/auto run    # Start autopilot with current policy
/auto off    # Immediately disable autopilot
```

Unlimited mode stops under the following conditions; bounded mode additionally pauses when the round budget is reached:

- Moderator explicitly requests a user decision.
- User interjects, pauses, changes the goal, or terminates the conference.
- Moderator confirms all agenda items are complete and concludes the conference.
- Bounded mode reaches the configured round limit, transitioning to `Paused`.
- Model or tool failures are recorded in the timeline; autopilot schedules the next member to continue. Users can pause to inspect.

Autopilot can be set to unlimited by the user but never automatically gains out-of-sandbox command permissions. Adjustments to rules, agenda, seats, or autopilot policy do not block or rewrite in-progress AI requests — they take effect on the next request. Free-text interjections and `/ask` immediately cancel the current streaming request, mark the temporary contribution as interrupted, and block subsequent automatic rounds. When output reaches the provider's length limit, the timeline marks `Output limit` and retains the reason; the system never writes incomplete structured directives into the agenda or decisions.

When conference history grows too long, the moderator compresses a small batch of earlier records into a persistent working summary, retaining the most recent raw events for subsequent contributions. The full timeline is never deleted and remains scrollable in Discussion; compression or failure both appear as timeline events. Conferences no longer enforce per-seat output token limits; all members use the global output cap from the current model configuration.

## 9. Reading Tools and Autopilot Events

The timeline records these key events:

- `tool_authorization`: Source and scope of single-round or autopilot pre-authorization.
- `tool_request`: Tool and arguments requested by a seat.
- `tool_result`: Result returned by the tool.
- `autopilot_policy`: User enabled, disabled, or modified autopilot policy.
- `autopilot_start`: Moderator begins an autopilot run.
- `autopilot_pause`: Autopilot waiting for user.
- `autopilot_limit`: Autopilot reached round budget.

### Delegate Subagent

Every conference seat can call `delegate_subagent` to delegate well-scoped verification, retrieval, or workspace operations to a short-lived execution subagent. The subagent receives only the calling seat's `task`, `deliverable`, and up to six brief `context` items (with total size limits) — it does not receive the conference timeline, other members' contributions, or the compacted conference summary.

Delegation is recommended for complex feature implementation, cross-file modifications, bug reproduction and fixes, long test chains, locating workspace evidence, or multi-step external searches. Tasks should specify target files or scope, expected deliverables, verification method, and necessary context. Once complete, the originating seat interprets the results and the moderator decides whether to adopt them. Simple judgments, short answers, and single lightweight queries should not incur subagent overhead.

Subagents inherit the calling seat's current-round permissions without gaining additional capabilities: a read-only seat's subagent can only use read-only tools; a user-authorized execution round can use the same full tool set; autopilot rounds remain constrained by the user's pre-authorized tool allowlist. Subagents cannot delegate further, cannot request privilege escalation, and cannot run commands outside the sandbox. Every delegation, its tool requests, results, final delivery, and interruptions are written to the timeline as `subagent_*` events.

Before confirming major decisions, executing file changes, or concluding a conference, review relevant tool events to confirm the actual execution scope and results match expectations.

## 10. Export and Conclusion

At any point before concluding, view the current summary:

```text
/summary
```

The summary includes goal, status, rounds, facts, open questions, decisions, and action items. It first presents the final conclusion from the last completed moderator contribution (with scheduling directives hidden), then lists participant seats and responsibilities, each agenda item's status and phase conclusion, user questions and answers, and completed advisor contributions. The summary uses an independent multi-line reader with arrow keys, PageUp/PageDown, Home/End navigation and does not render newlines as control characters. Even when members omit structured tags like `FACT:` or `DECISION:`, original discussion contributions are preserved in the summary; full records beyond the display cap are viewable in the timeline.

Export a Markdown summary:

```text
/export release-plan.md
```

The export path must be relative to the conference workspace. Without a specified path, the system uses `<conference-ID>-summary.md`.

When discussion is complete, select `Conclude meeting` in the control panel. The system consolidates remaining agenda items, generates completion status, and preserves the full timeline. To stop without marking as complete, select `End conference` or use `/end`.

## 11. Recommended Practices

For higher-risk or workspace-modifying goals, adopt the following rhythm:

1. First use manual, read-only rounds to let experts and auditors form candidate solutions.
2. Use `/decision` to explicitly confirm, reject, or request evidence for candidate decisions.
3. Then configure autopilot, starting with 4 rounds and no write permissions.
4. Only when action items are sufficiently concrete, check the minimum required tools.
5. After auto-runs, inspect tool events and summaries; interject to correct scope if needed.
6. Export the summary and confirm that facts, risks, action items, and open questions are all recorded before concluding.

This approach retains the efficiency of multi-AI discussion while keeping the goal, authorization, and final decisions always under user control.
