# AI Conference 

## 1. and 

AI Conference（AI conference） "User-AI" 。User goal（goal） and ， responsibilities AI goal 、 、 propose 、 risks ， conclusion and action items。

 Ordinary directly and ；conference 、 、evidence 、task or decisions 。conference output ， agenda、rules、Status、User and artifact 。

User has 。Usercan 、 facts、 goal、 、CONTINUE、 rules、 、confirmation or decisions， and conference。 AI rules、 or tool 。

## 2. Usergoal and conference 

 conference ，User ：

- goal：conference specificquestion。
- and ： 、 、 、 、 or conclusion。
- ： 、decisions、 risks 、 、 or Meeting Minutes。
- ： 、 、 、 、 、 and 。
- or ：User / ， AI 。

conferencecomplete must output ， final 。 to ：goal、 and and responsibilities、 using rules、agenda 、 Verification facts、 、candidate 、finaldecisions、 、 、 risks 、action items and question。 evidence or ， " Verification facts"、" " and " "。

## 3. 

conference AI ， and 。 responsibilities ，do not ； can ， must 。

- Moderator： goal，generated and agenda and rules，schedule ， ，decided evidence ， 。
- Expert： 、 、 、 、 、 or question， propose and 。
- Auditor/ ： 、 、 risks 、 and ； candidatedecisions has 。
- ： question using authorization tool evidence，report through facts、 and ， conclusion。
- line ： User authorization ， confirmed action items for 、 line or 。
- Recorder： facts、Open questions、decisions、 、evidence and action items； Meeting Minutes， conclusion。

Moderatorcan goal 、 or ， must reason、 responsibilities and agenda 。

## 4. rules and agenda

conferencestart ， AI goal propose 、 rules and agenda。Usercan directlystart， can 。rules System ； 、 、 and 。

rules to ：

- rules： 、Moderator 、Expert 、 。
- rules： question、 ， and 。
- ： 、 、 risks 、words or Rounds 。
- decisionsrules： 、 、 、 or must Userconfirmation。
- ： risks 、 line 、 if the evidence， or through 。
- tool ： tool line， must Moderator or User 。

 default rules can ：Moderatordesignates speakers； , no more than 180 words； conclusionmust or ；Auditor candidatedecisions ； evidence ； use must Userconfirmation。

agenda if the Status agenda items 。 agenda items 、 、 、 、 Status、conclusion and question。Status using ： start、 line 、 evidence、 Userdecided、 complete、 Blocker、 。Moderator END agenda items and ， Recorder conference facts and artifact。

## 5. conference line and Status 

conferenceStatus using User 、 ：

```text
Draft -> Preparing plan -> Awaiting plan approval -> Running <-> Paused
 | |
 +-> Awaiting user decision
 |
 Concluding -> Completed
 |
 Stopped
```

- `Preparing plan` / `Awaiting plan approval`：Moderator #0 using propose conference 。 #0 、#1 to #N advisor seat， Moderator for specific 、 、 、 using providers and 、 depth and Rounds ；User ， schedule AI 。
- `Running`： rules 、 line or tool 。
- `Paused`： schedule ；User 、 rules and agenda、CONTINUE or END。
- `Awaiting user decision`： goal 、 、 request or Userconfirmation decided ，conference question and 。
- `Concluding`：Moderator for candidateconclusion、 and action items；User 、 or CONTINUE 。
- `Completed`： generated， 、CONTINUEconference or Status conference。
- `Stopped`：User ； ， complete agenda items for 。

 Rounds for ：Moderator #0 agenda items -> advisor seat propose 、evidence or -> System Status and -> through Moderator #0 evaluates decided 。 conference output for ， 、 、bold、italic、code fences、inline code、quotes、 and Markdown format is strictly forbidden； linemust directly `FACT:`、`QUESTION:`、`DECISION:`、`ACTION:` or Moderator 。Moderator advisor seat output depth ；advisor seat propose `SUGGEST_NEXT`， final Moderator or User。Moderator `NEXT_SPEAKER` must using advisor seat； missing System 。

## 6. User interjection and 

User conference 。User Status Ordinary default "User interjection"， line or 。 for User ， start ；Moderator must goal、 facts、rules、agenda and conclusion ， CONTINUEconference。

Usercan ：

- or facts、 、 or 。
- ， or 。
- 、 、 or agenda items 。
- 、CONTINUE or conference。
- 、 、rules and tool 。
- confirmation、 、 or CONTINUE candidatedecisions。

Moderator Userquestion。 question 、 （ 、 or ）、 options、 propose 、 、 、Status and 。 Moderatorable to conference Status； Moderator propose question 。 END：System 、 Moderator User ， Moderator 、 or agendaCONTINUEadvance。

 User goal or ，Moderatormust conclusion 、 、agenda 。User 、goal or request start tool using ； line task request for " User "； must Status and complete 。

## 7. tool using and authorization 

tool conferenceadvance and facts ， default using 。 default ， can propose toolrequest；Moderator conferencerules risks 、 ； use 、 、 and must Userconfirmation， User authorization 。

### 7.3 Moderator advance

conference using “Moderator advance”。Moderator schedule 、advance agenda items schedule authorization facts 。 default for 12 ，TUI 、4、8、12 or 20 ； through 。 Moderator request Userdecided、Moderatorcompleteconference、User / or conference END 。 Ordinary candidatedecisions、Open questions、evidence and Moderator for specifictask， reason。

 default tool。Usermust using conference using ：`write_file`、`run_command`、`fetch_http`、`browse_page`、`web_search`。 authorization conference、 tool and Rounds ； 、 start and tool using use conference 。tool ， authorization using 。

 User authorization `run_command`， line， request or 。User or using `/auto off` advance； Ordinary Rounds UserStatus。 line or tool using complete ， schedule 。

### 7.1 using tool 

- ：Moderator User 、 and taskStatus。 default line or 。
- facts ： through must evidence advance question ，Moderator using 、 、 、data or tool。
- ： line 、check 、 line 、 Status or ， Verification line 。
- confirmedaction items line：Userconfirmation line ， line only 、 line 、 task or ； line must specificaction items。
- Verification： line ， line or Auditor using 、check and toolconfirmation 。failed agenda items through ， directly complete。
- ：Recorder ；generated or using 。

### 7.2 tool using 

 tool using are conference ， to request 、 、 agenda items 、 、 、 and Status。tool for evidence， /failed/ Status、 output 、 output 、 line 、 line 、 using and agenda items 。

 using ：requeststart “ ” ， through token 。 END for complete， `FACT:`、`QUESTION:`、`DECISION:`、`ACTION:` and Moderator `AGENDA:` / `AUTOPILOT:` 。 failed through content for complete， use facts、decisions or agendaStatus。if the providers END， `Output limit`、END reason and output ， 。User ， ； through 。

conference using Moderator 。 through ，Moderator for auditable ； through and 。 or use ，failed failed 。 `response_token_limit`， using configuration output 。

AI and tool task line，TUI 。User rules、goal、agenda、seat、 、 depth、decisions and advance ， request ； use or start 。 Ordinary and `/ask` ：System 、 request、 for ， for User。if the occurs tool line ， tool complete， tool or Rounds。

 confirmation request TUI for ， ：

```text
[ ] requesttool： line 
 ：Verification A API
 ： / 2 

> line
 
 
```

rules line risks request authorization ， "Moderator Tool policy v2 "。toolfailed、 or for risks or Blocker ， for Ordinary content。

## 8. TUI information 

 using ： for "conference "， for ， for ； 。 、 Status and ， 、 。

```text
+ AI Conference - project-release-plan - Running - Round 2/6 ------------+
| Goal: ， risks and line |
+--------------------+--------------------------------------+----------------+
| Agenda & State | Discussion | Controls |
| > 1. [x] | [Moderator][Current agenda item: ] | > conference |
| 2. [*] | line ... | |
| 3. risks [ ] | | agenda items |
| | [ ][ ][ ： ] | / rules|
| Facts | A ... | decisions |
| [x] | | ENDconference |
| [?] | [Auditor][ ] | |
| | A ... | Status： |
+--------------------+--------------------------------------+----------------+
| or ... /help |
+--------------------------------------------------------------------------+
```

### 8.1 Status 

 conference 、goal 、conferenceStatus、 Rounds、 agenda items and /tool。Statusmust using words ， "Running: Auditor "、"Paused"、"Awaiting user decision"。

### 8.2 ：conference 

 "conference through "。 default agenda、 facts、Open questions、decisions、 and action items。 agenda items Status words and ：`[x]` complete、`[*]` line 、`[ ]` start、`[!]` Blocker、`[-]` 。 Rounds for ，User through conclusion。

### 8.3 ： 

 default 。 、 、Rounds or 、content and 。 to " "、"evidence"、" "、" "、" "、"tool" and "User"。 default ；evidence、 、tool output and quotes using " + " 。User using and " "Status， Ordinary 。

### 8.4 ： 

 ， 。 ： /CONTINUE、 、 agenda items 、 or rules、 decisions、ENDconference。 or Status confirmation ， default " " or options 。

### 8.5 rules、decisions and 

rules and 。rules or agenda System ，User ， 。User rules ： Rounds， use 。

candidatedecisions using ， decisions 、 、 risks 、 and action items。Usercan confirmation、 、 evidence or CONTINUE ； for Status 。

## 9. 

 and Enter ，User complete 、 、 、 、decisions and ENDconference。 must or ；Status 。

| | line for |
|---|---|
| `Left` / `Right` | conference 、 、 and 。 |
| `Up` / `Down` | 、 and decisionsoptions / ； 。 |
| `Enter` | agenda items or ， line default ， or confirmation options。 |
| `Esc` | ， 、 or ， ； directlyQUITconference。 |
| `Space` | /CONTINUE conference ； / 。 |
| `Tab` / `Shift+Tab` | or ， for 。 |
| `Home` / `End` | to or / 。 |
| `PageUp` / `PageDown` | or 。 |
| `?` | 、 。 |

 `Right` or `i` directly 。 Ordinary default for User interjection；`Enter` ，`Shift+Enter` line，`Esc` content through 。 for `Enter` 。

 and words for ， must ：

- `/say <content>`： ， directly 。
- `/ask < > <question>`： 。
- `/focus < agenda items >`： 、 or agenda items 。
- `/pause`、`/resume`、`/end`： conference 。
- `/rule` and `/rule edit`： or rules。
- `/summary`：generated Status ， ENDconference。
- `/auto [run|on|off]`： line、 using or Moderator advance 。
- `/autopilot`： using 、Space and Enter configuration Rounds and tool authorization 。
- `/decision`： candidatedecisions 。
- `/export`： 、decisions and action items。

candidatedecisions and Enter：

```text
Candidate decision: choose phased rollout

> Confirm decision
 Request more evidence
 Continue discussion
 Reject

[Up/Down ] [Enter confirmation] [Esc ]
```

conference using ：`Up`/`Down` or `Tab` words ，`Enter` ，`Space` or ， "startconference" `Enter` 。 using `F1` Status、`F2` agenda、`F3` ， Status and 。

## 10. data and 

 Status， 。 ：

- `Conference`：id、title、goal、owner、status、createdAt、updatedAt、 and Rounds。
- `Participant`：AI 、 、 、 、 、tool and Status。
- `RuleSet`： 、 rules、 、Rounds/ 、decisions 、 and tool 。
- `AgendaItem`： 、 、 、 、Status、conclusion、 question and 。
- `Message`： 、 （AI/User/System）、 、 、Rounds、 、quotes、 and Status。
- `Evidence`： 、 、 line 、tool using 、 、 output quotes、 using and 。
- `Decision`：candidate/confirmed/ Status、 、 risks 、confirmation 、 evidence and 。
- `ActionItem`：task、 、 authorization Status、 lineStatus、Verification and decisions。
- `InterruptEvent`：User interjection、 、 、 and /tooltask。
- `ToolRequest` / `ToolRun`：request 、 、 authorization 、 Status、 lineStatus and 。

 conferenceStatus ， "rules "、" agenda items "、"toolrequest"、"User "、"decisionsconfirmation" and "taskVerification"。 conferencecan 、 、 and ， 。

## 11. MVP and 

 ：

1. AI and 2 through 3 responsibilities ， using line 。
2. rules and agenda。
3. conference TUI：conference 、 、 、 and 。
4. User 、CONTINUE、END、 decisions and use directly 。
5. Moderator toolrequest、Userconfirmation use ， and tool and 。
6. facts、decisions、 、action items and final 。

 MVP ： and 、 line 、 and evidence 、 、conference 、 conference 、 conference、 and ， and conclusion and and 。

 conference 、 、auditable ， and line 。
