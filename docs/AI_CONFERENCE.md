# AI Conference Specification

## 1. Scope and Boundaries

AI Conference is a collaborative workflow independent of basic user-AI conversations. The user provides a goal and necessary context; multiple AI participants with clearly defined responsibilities discuss, verify, propose solutions, review risks, and produce traceable conclusions and action items.

Ordinary chat suits direct Q&A and one-on-one collaboration; conferences suit work requiring multi-perspective analysis, solution comparison, evidence verification, task decomposition, or formal decision-making. A conference is not a chat log of roles taking turns — it is a controlled collaboration process with an agenda, rules, state, user control, and structured deliverables.

The user always has the highest priority. The user may interject at any time, correct facts, modify the goal, pause, resume, adjust rules, require specific members to answer, confirm or reject decisions, and terminate the conference. No AI rule, automated orchestration, or tool permission may prevent these actions.

## 2. User Goals and Conference Deliverables

When creating a conference, the user may provide:

- Goal: the specific problem the conference should solve.
- Background and input materials: requirements, code, logs, documentation, links, or existing conclusions.
- Expected deliverables: recommendations, decisions, risk assessments, implementation plans, code changes, or meeting minutes.
- Constraints: time, cost, tech stack, permissions, scope, unacceptable approaches, and stopping conditions.
- Optional members or templates: the user may specify members/roles or delegate to the moderator AI for automatic assembly.

Upon completion, the conference must output structured minutes, not just a final paragraph of text. Minutes must include at minimum: goal, participants and their responsibilities, rules adopted, agenda summary, verified facts, key assumptions, candidate solutions, final decisions, primary rationale, reserved objections, risks, action items, and unresolved questions. Each item should identify its evidence or discussion source and distinguish "verified fact," "reasonable inference," and "unverified suggestion."

## 3. Role Model

The conference is organized by a moderator AI. Participants assume the following roles as needed. Roles are responsibility sets, not tied to fixed models; the same model may serve multiple roles when necessary, but the interface must clearly show the current speaking identity.

- Moderator: clarifies the goal, generates and maintains agenda and rules, schedules contributions, controls discussion scope, decides whether evidence verification is needed, and drives convergence.
- Expert: analyzes problems from architecture, domain knowledge, performance, security, cost, product, or other designated perspectives, proposing solutions with supporting evidence.
- Auditor / Devil's Advocate: actively seeks missing assumptions, counterexamples, risks, rollback costs, and alternatives; holds a formal objection opportunity before candidate decisions.
- Researcher: uses authorized read-only tools for well-defined questions to gather evidence, reporting observed facts, scope, and limitations rather than conclusions alone.
- Executor: after user authorization, converts confirmed action items into file modifications, command execution, or other concrete work.
- Recorder: maintains facts, open questions, decisions, objections, evidence, and action items; compiles meeting minutes without generating undiscussed conclusions.

The moderator may add, replace, or merge roles based on the goal, but must record the reason, responsibility changes, and impact on the current agenda.

## 4. Rules and Agenda

Before the conference begins, the moderator AI proposes a visible, editable initial rule set and agenda based on the goal. The user may start directly or modify them first. Rules must not be hidden in system prompts; every change should show version, diff, initiator, and effective scope.

The rule set covers at minimum:

- Speaking rules: round-robin, moderator-designated, expert-first, propose-then-challenge, etc.
- Scope rules: the problem to solve, permitted related extensions, and explicitly excluded topics.
- Contribution quality: whether evidence, confidence levels, risk statements, word or round limits are required.
- Decision rules: moderator ruling, consensus-first, reserved objections, voting, or mandatory user confirmation.
- Convergence conditions: key risks addressed, solution has executable steps, consecutive rounds without new evidence, or budget reached.
- Tool policy: which tools may run automatically, which require moderator or user approval.

A default rule set might be: moderator designates speakers; each person 180 words per round; key conclusions must state basis or assumptions; auditor has one objection opportunity before each candidate decision; two consecutive rounds without new evidence triggers convergence; write operations must be confirmed by the user.

The agenda consists of stateful agenda items. Each item has a title, inputs, owner, dependencies, current status, conclusion, and unresolved questions. Statuses are: not started, in progress, awaiting evidence, awaiting user decision, completed, blocked, cancelled. The moderator updates the current item and next steps after each round; the recorder synchronously updates conference facts and deliverables.

## 5. Conference Operation and State Machine

Conference state uses only user-understandable, actionable phases:

```text
Draft -> Preparing plan -> Awaiting plan approval -> Running <-> Paused
                                                    |              |
                                                    +-> Awaiting user decision
                                                                   |
                                                   Concluding -> Completed
                                                                   |
                                                                Stopped
```

- `Preparing plan` / `Awaiting plan approval`: Moderator #0 proposes a reviewable meeting plan via a real model call. The plan explicitly includes moderator seat #0 and advisor seats #1 through #N, with the moderator specifying name, role, responsibility, enabled provider and model, discussion depth, and round budget for each seat. The user may adjust each seat before approving; no AI contributions may be scheduled before approval.
- `Running`: Members discuss according to rules, execute approved read-only verification, or await tool results.
- `Paused`: New contributions are not scheduled; the user may still view records, edit rules and agenda, resume, or end.
- `Awaiting user decision`: When goal conflicts, irreplaceable trade-offs, permission requests, or decisions requiring user confirmation arise, the conference explicitly pauses and lists the question with options.
- `Concluding`: The moderator compresses into candidate conclusions, objections, and action items; the user may still cancel, interject, or request further discussion.
- `Completed`: Minutes have been generated; read-only review, continuing the conference, or creating a branch conference from the current state is possible.
- `Stopped`: User actively terminated; existing records are preserved, unfinished items marked as stopped.

Normal round flow: Moderator #0 briefly declares the current agenda item and assigns a member → the designated advisor seat proposes, provides evidence, or objects → the system records structured state and optional next-seat suggestions → return to Moderator #0 to evaluate disagreements and decide the next round. All conference member output is plain text, forbidding headings, bullet lists, bold, italic, code fences, inline code, quotes, links, and tables — any Markdown format. Structured lines must begin directly with `FACT:`, `QUESTION:`, `DECISION:`, `ACTION:`, or moderator directives. The moderator does not produce deep technical proposals on behalf of advisors; advisors may propose `SUGGEST_NEXT`, but final scheduling authority rests solely with the moderator or user. The moderator's `NEXT_SPEAKER` directive must specify an enabled advisor seat; if missing, the system records a visible scheduling fallback event.

## 6. User Interjections and Interruptions

User messages are the highest-priority events in a conference. Plain text entered by the user in any state defaults to a "user interjection" without requiring a prior command or mode switch. The interface immediately displays it as a user event and suspends unscheduled contributions; the moderator's next step must first explain the message's impact on the goal, facts, rules, agenda, and existing conclusions before resuming.

The user may:

- Supplement or correct facts, add context, modify constraints or deliverables.
- Name a specific member to answer, require comparison of options, or explain a basis.
- Switch, add, skip, or reopen agenda items.
- Pause, resume, or terminate the conference.
- Modify members, roles, rules, and tool policies.
- Confirm, reject, request evidence for, or request continued discussion of candidate decisions.

The moderator may initiate structured user questions. Each question contains: body, type (subjective, objective, or mixed), options, requester, creation time, timeout, status, and answer. Only the moderator may transition the conference into the awaiting-answer state; other members may only suggest questions to the moderator. Timeout is not silent: the system records a timeout event, explicitly prompts the moderator in subsequent context that the user did not answer, and has the moderator proceed with conservative assumptions, additional verification, or agenda adjustments.

When the user modifies the goal or key constraints, the moderator must explicitly note which prior conclusions remain valid, which need reexamination, and how the agenda will adjust. User pause, goal-change, or cancellation requests cancel tool invocations not yet started; cancellable in-progress tasks should request stop and be marked "cancelled due to user intervention"; external operations that cannot be safely aborted must display current status and estimated completion boundary.

## 7. Tool Use and Authorization

Tools are a means for conference progress and fact verification, not a capability every member can freely invoke by default. By default, members may propose tool requests; the moderator approves low-risk, well-scoped read-only operations according to conference rules; write operations, external communications, irreversible operations, and access beyond established scope must receive user confirmation unless the user has explicitly pre-authorized that category and scope of operation.

### 7.3 Moderator Intelligent Autopilot

The conference may enable "moderator autopilot." The moderator automatically schedules member contributions, advances the agenda, and arranges authorized fact verification. The default budget is 12 rounds; the TUI offers unlimited, 4, 8, 12, or 20 rounds. Bounded mode pauses when the budget is reached. Unlimited mode only stops when the moderator explicitly requests an irreplaceable user decision, the moderator completes the conference, the user interrupts/pauses, or the conference is terminated. Ordinary candidate decisions, open questions, evidence gaps, and member disagreements should be converted by the moderator into specific tasks for the next seat rather than reasons to stop.

Autopilot defaults to read-only tools only. The user must individually check which capabilities may be auto-used in this conference before enabling: `write_file`, `run_command`, `fetch_http`, `browse_page`, `web_search`. Authorization is valid only for the current conference, the currently selected tools, and subsequent bounded autopilot rounds; every policy change, round start, and tool call is written to the conference timeline. The tool layer enforces this allowlist; model-fabricated unauthorized calls are also refused.

Even if the user pre-authorizes `run_command`, automatic commands still run in the workspace sandbox; autopilot mode cannot request or receive out-of-sandbox privilege escalation. The user may immediately revoke autopilot via the control menu or `/auto off`; free-text interjections cause subsequent automatic rounds to transition to awaiting user. A synchronously executing single model or tool call will complete its current safety boundary before scheduling of the next round stops.

### 7.1 When Tools May Be Used

- Pre-conference preparation: the moderator may read materials explicitly provided by the user, current project context, and existing task status. No modifications or external actions by default.
- Fact verification: when discussion reaches a point where evidence is required to proceed, the moderator designates the researcher to use file reading, code search, log inspection, database queries, or retrieval — read-only tools only.
- Solution evaluation: members may run read-only analysis, check dependencies, execute tests, inspect version status, or estimate resource consumption to verify the feasibility of a specific proposal.
- Confirmed action item execution: only after user confirmation may the executor edit files, run migrations, create tasks, or send external messages; execution scope must be bound to a specific action item.
- Verification: after execution, the executor or auditor uses testing, inspection, and review tools to confirm results. Failure returns the item to discussion; it must not be declared complete.
- Wrap-up: the recorder may only organize existing materials; generating minutes should not trigger additional high-cost or side-effecting operations.

### 7.2 Tool Call Events

Every tool call is a visible conference event, showing at minimum: requesting member, purpose, associated agenda item, impact scope, permission level, expected duration, and current status. Tool results are backfilled as expandable structured evidence containing success/failure/cancelled status, key output summary, full output entry point, execution time, executor, applicable scope, and impact on the current agenda item.

Conference members may use `delegate_subagent` to create short-lived execution subagents for specific operations whose context should not be polluted by the long conference record. Call parameters may only include task, deliverable criteria, and a limited amount of necessary context; the subagent does not receive conference history and cannot delegate further. It inherits the initiating seat's current-round model, provider, read-only/full tool permissions, and autopilot allowlist, but must not request out-of-sandbox privilege escalation. The subagent's task, each tool call, output, final deliverable, failure, and user cancellation are all preserved as independent timeline events and backfilled as a tool result to the initiating seat.

Member contributions use streaming timeline: a "speaking" event is created when the request starts, then content is continuously appended as tokens arrive and saved. The event is marked complete only after the stream ends, at which point `FACT:`, `QUESTION:`, `DECISION:`, `ACTION:`, and moderator `AGENDA:` / `AUTOPILOT:` directives are parsed in bulk. Network failures preserve received content and mark it incomplete; partial text is never written into facts, decisions, or agenda state. If the provider ends due to length limit, the event shows `Output limit`, the end reason, and the current global output cap — partial directives are similarly not parsed. When the user browses old records, the timeline pauses auto-follow; returning to the end resumes following live contributions.

Conference context uses moderator-driven incremental compaction. When earlier events reach a threshold, the moderator compresses a small batch of early records together with the existing working memory into a new auditable summary; subsequent members receive this summary plus recent uncompacted events. Compaction never deletes or rewrites the original timeline; on failure, the original context is retained and a failure event is recorded. Members do not have independent `response_token_limit` values; all use the global output cap from the current model configuration.

AI contributions and tool loops run in background tasks; the TUI remains operable at all times. User modifications to rules, goal, agenda, seats, models, discussion depth, decisions, and autopilot policy are saved immediately and take effect when the next model request constructs its context; in-progress contributions are not rewritten or discarded. Free-text interjections and `/ask` are high-priority interrupts: the system immediately records the interjection, cancels the current streaming request, marks the temporary contribution as aborted, and transitions to awaiting user. If an interrupt occurs during a non-cancellable local tool run, the current tool may complete safely, but no subsequent tool or model rounds will be launched.

Requests requiring confirmation appear in the TUI as selectable panels, for example:

```text
[Architect] requests tool: run current test suite
Purpose: verify whether Plan A breaks the existing API
Impact: read-only / estimated 2 minutes

> Run now
  View details
  Reject
```

Low-risk requests already permitted by rules must still show their authorization source, e.g., "Moderator approved per Tool policy v2." Tool failures, timeouts, or cancellations should become risk or blocking events, not hidden as ordinary chat content.

## 8. TUI Information Architecture

Wide terminals use a three-pane main interface: left pane is the "conference map," center pane is the discussion timeline, right pane is the control menu; the bottom always retains the input area. Narrow terminals retain the center discussion area, top status, and bottom input; left and right panes are accessed via full-screen drawers.

```text
+ AI Conference - project-release-plan - Running - Round 2/6 ------------+
| Goal: Create release plan prioritizing risk control and 2-week feasibility |
+--------------------+--------------------------------------+----------------+
| Agenda & State     | Discussion                           | Controls       |
| > 1. Constraints[x]| [Moderator][Current item: trade-offs]  | > Pause        |
|   2. Trade-offs [*]| We have two viable approaches...      |   Interject    |
|   3. Risk audit [ ]|                                      |   Summarize    |
|                    | [Architect][Proposal][Confidence: high]|   View/Edit    |
| Facts              | Plan A has delivery speed advantage... |   Rules        |
| [x] 2-week window  |                                      |   Decisions    |
| [?] Security scope | [Auditor][Objection]                   |   End Meeting  |
|     TBD            | A ignores rollback rehearsal time...   |                |
+--------------------+--------------------------------------+----------------+
| Input interjection or command...                                 /help    |
+--------------------------------------------------------------------------+
```

### 8.1 Top Status Bar

Continuously displays conference name, goal summary, conference status, current round, current agenda item, and member/tool being awaited. Status must use text rather than color alone, e.g., "Running: awaiting auditor response," "Paused," "Awaiting user decision."

### 8.2 Left Pane: Conference Map

The left pane answers "where is the conference." It defaults to showing the agenda, current facts, open questions, decisions, objections, and action items. Agenda items have both text and symbol status: `[x]` completed, `[*]` in progress, `[ ]` not started, `[!]` blocked, `[-]` cancelled. Older rounds are collapsed to summaries in the timeline; the user can see current conclusions in the left pane without scrolling through long records.

### 8.3 Center Pane: Discussion Timeline

The center pane is the default focus. Each message consistently shows role, message category, round or time, content, and optional confidence. Categories include at minimum "proposal," "evidence," "objection," "ruling," "record," "tool," and "user." Model internal reasoning is not shown in the default view; evidence, code, tool output, and citations use "summary + expand" form. User messages use a high-contrast style and "interrupted current flow" status so they are not buried among ordinary messages.

### 8.4 Right Pane: Control Menu

The right pane is a focusable vertical operation menu, not merely a display of non-clickable shortcut hints. High-frequency operations are always visible: pause/resume, interject and ask, summarize current item, view/edit rules, view decisions, end conference. Dangerous or state-changing operations enter a confirmation overlay with default focus on "cancel" or another safer option.

### 8.5 Rules, Decisions, and Detail Panels

The rules drawer shows the current version with concise entries. Rule or agenda changes produce system events in the timeline; the user may view diffs and, where permitted, undo the adjustment. When editing rules, the user is explicitly reminded: changes only affect subsequent rounds and do not rewrite history.

Candidate decisions use an independent focusable panel containing: decision text, supporting evidence, primary risks, reserved objections, and suggested action items. The user may confirm, reject, request additional evidence, or continue discussion; these actions are all recorded as formal state changes.

## 9. Keyboard-First Operation

Arrow keys and Enter are first-class interaction methods; the user need not memorize commands to create, browse, interject, pause, decide, and end a conference. The current focus must have a visible border or inverse highlight; the status bar shows a brief context hint as focus changes.

| Key | Behavior |
|---|---|
| `Left` / `Right` | Switch focus between conference map, discussion timeline, control menu, and input bar. |
| `Up` / `Down` | Select previous/next item in lists, menus, and decision options; scroll discussion. |
| `Enter` | Open selected agenda item or detail, execute current safe default action, or confirm selected option. |
| `Esc` | Cancel edit, close detail, drawer, or overlay, return to previous level; does not directly exit the conference from the main interface. |
| `Space` | Pause/resume automatic conference flow in the main discussion area; expand/collapse selected item in lists. |
| `Tab` / `Shift+Tab` | Move focus forward/backward in logical order, complementing arrow-key navigation. |
| `Home` / `End` | Jump to start/end of current list or discussion record. |
| `PageUp` / `PageDown` | Scroll through long timelines or long lists by page. |
| `?` | Open complete, scrollable contextual help. |

After gaining focus via `Right` or `i`, the input bar accepts direct text. Plain text defaults to user interjection; `Enter` sends, `Shift+Enter` inserts a newline, `Esc` discards unsent content and returns to the discussion area. Pressing `Enter` on an empty input bar must not trigger a dangerous action.

Slash commands and letter shortcuts are efficiency supplements only and must be discoverable from help:

- `/say <content>`: Send interjection, equivalent to direct input.
- `/ask <role> <question>`: Name a member to answer.
- `/focus <item>`: Switch, add, or reopen an agenda item.
- `/pause`, `/resume`, `/end`: Control conference lifecycle.
- `/rule` and `/rule edit`: View or edit rules.
- `/summary`: Generate current state summary without ending the conference.
- `/auto [run|on|off]`: Run, enable, or disable saved moderator autopilot policy.
- `/autopilot`: Configure auto-rounds and per-tool pre-authorization with arrow keys, Space, and Enter.
- `/decision`: Open candidate decision panel.
- `/export`: Export minutes, decisions, and action items.

The candidate decision panel fully supports arrow keys and Enter:

```text
Candidate decision: choose phased rollout

> Confirm decision
  Request more evidence
  Continue discussion
  Reject

[Up/Down to select] [Enter to confirm] [Esc to cancel]
```

The conference creation page likewise uses form navigation: `Up`/`Down` or `Tab` to switch fields, `Enter` to open selectors, `Space` to toggle members or templates, focusing "Start Conference" then `Enter` to launch. On narrow terminals, `F1` status, `F2` agenda, `F3` controls open the respective drawers, while the top status and input bar remain always visible.

## 10. Data and Event Model

The implementation should preserve structured state, not just rendered text. Suggested core entities:

- `Conference`: id, title, goal, owner, status, createdAt, updatedAt, budget, and current round.
- `Participant`: AI identifier, display name, role, capability tags, speaking permissions, tool permissions, and status.
- `RuleSet`: version, speaking rules, discussion scope, round/cost budget, decision mechanism, convergence conditions, and tool policy.
- `AgendaItem`: title, inputs, owner, dependencies, status, conclusion, unresolved questions, and associated events.
- `Message`: author, author type (AI/user/system), role, category, round, body, citations, confidence, and status.
- `Evidence`: source, time, executor, tool call, summary, full output reference, applicable scope, and reliability note.
- `Decision`: candidate/confirmed/rejected status, rationale, risks, confirmer, associated evidence, and objections.
- `ActionItem`: task, assignee, authorization status, execution status, verification result, and associated decision.
- `InterruptEvent`: user interjection, priority, impact scope, handling result, and cancelled orchestration/tool tasks.
- `ToolRequest` / `ToolRun`: request purpose, impact scope, authorization basis, approval status, execution status, and result.

All conference state changes are recorded as appended events, such as "rule update," "agenda switch," "tool request," "user interrupt," "decision confirmation," and "task verification." This allows conferences to be resumed, audited, exported, and branched from a point in time without tampering with history.

## 11. MVP Scope and Future Evolution

The first version should prioritize a complete control loop:

1. One moderator AI with 2–3 fixed-role participants using serial turn-taking.
2. Visible and editable initial rules and agenda.
3. Main conference TUI: conference map, discussion timeline, control menu, keyboard-first navigation, and real-time interjection.
4. Direct user control over pause, resume, end, major decisions, and write operations.
5. Moderator-approved read-only tool requests, user-confirmed write operations, and visible tool events with results.
6. Structured facts, decisions, objections, action items, and final minutes.

After MVP stability, expand to: dynamic role recruitment and capability matching, parallel breakout discussions, voting and evidence scoring, controlled shared workspaces, conference templates, historical conference search, branch conferences, cost and time observability, and evaluation of conclusion quality and participant contribution.

This priority ordering ensures the conference is first and foremost a controllable, interruptible, auditable collaboration workflow, with automation and parallelism increasing gradually.
