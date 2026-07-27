# AI Conference 

AI Conference Ordinary “User-AI” 。 goal ，Moderator、Expert、Auditor and Recorder agenda items 、 、 risks conclusion。

 、 、 advance、authorized tool and 。 [AI_CONFERENCE.md](AI_CONFERENCE.md)。

## 1. start 

 complete providersconfiguration：

```sh
cmake -S . -B build-conference
cmake --build build-conference
./build-conference/ask --config
```

 configuration to using using providers， default 。AI Conference using providerscomplete and tool using 。

 、 、 risks or line goalstart。 ：

```sh
./build-conference/ask conference " for ， line "
```

 providers and ：

```sh
./build-conference/ask conference " " --provider deepseek --model deepseek-v4-flash
```

## 2. and conference

### conference

 line `ask conference "goal"` conference 。 conference ：

- Moderator： goal、 agenda、 and 。
- Expert： propose 、 and line 。
- Auditor：check risks 、 、 and path。
- Recorder： facts、candidatedecisions、Open questions and action items。

 agenda goal 、candidate 、 risks and action items。 `Preparing plan`：Moderator #0 using generated ，specificdecided #0 through #N 、 、 、providers and ；Usermust `Review meeting plan` confirmation or ，conference start。Moderator using providers ， using providers through 。conference and Output plain text， using Markdown。

 ：

- `#0` Moderator， agenda 、 evaluates、 and ， advisor seat output depth 。
- `#1` through `#6` for advisor seat ； 、 responsibilities、 、 ID、 using Status and 。
- depth for `quick`、`standard`、`deep` or `audit`， agenda items 4、8、16、24 check 。 through check Moderatormust evaluates 、 Phase conclusion， decidedCONTINUE or agenda items ； conference。

 using words 、Enter 。confirmation `Approve plan and start meeting` ， Moderator #0 start schedule； line `Meeting parameters` or `/setup` ， requeststart 。

### Moderator User 

Moderator User advance 、 authorization 、 facts or 。question for 、 or ； / options， question 30 through 24 。`Awaiting user` `Answer moderator question`， using and Enter ， directly 。 directly 。

advisor seat directly User ， requestModerator question；Moderator 、 options and 。User 、 and use 。 conference “User ” Moderator， missing information、 using or scheduleagenda； using advance CONTINUE 。

### conference

```sh
./build-conference/ask conference resume
```

 using conference Enter 。conference data `conferences` ， Status、agenda、 、tool and advance 。

## 3. and Status

 for ：

- `Agenda & State`：agenda、confirmed facts and Open questions。
- `Discussion`： 、User interjection、SystemStatus and tool 。
- `Controls`： conference 。
- ： Ordinary or 。

 ， using `F1`、`F2`、`F3` 、agenda and 。

conferenceStatus ：

| Status | | |
| --- | --- | --- |
| `Awaiting plan approval` | Moderator propose seat、 and depth ， User | `Review meeting plan`， or |
| `Running` | schedule AI | CONTINUEadvance、 or |
| `Paused` | schedule | CONTINUE、 or END |
| `Awaiting user` | User 、decisions or Open questions | CONTINUE |
| `Completed` | generatedconferenceconclusion | or |
| `Stopped` | User | |

## 4. 

conference for ， can complete 。

| | |
| --- | --- |
| `Left` / `Right` | agenda、 、 and |
| `Up` / `Down` | 、agenda ， or |
| `Enter` | line 、confirmation or |
| `Space` | or CONTINUEconference |
| `Tab` / `Shift+Tab` | or |
| `Home` / `End` | through or |
| `PageUp` / `PageDown` | |
| `F1` / `F2` / `F3` | 、agenda、 |
| `i` | |
| `?` | |
| `Esc` | 、 or conference |
| `f` | ； `End` |

 ， REPL：`Up` / `Down` ，`Tab` ，`Ctrl-U` 。 Ordinary words （ UTF-8 、 and `?`） ， or 。

 ， using `Up` / `Down` 、`Enter` line。 ， ENDconference or authorized tool， confirmation ， default 。

## 5. conference 

 using advance ：

1. `Review meeting plan`， Moderator #0 numbersseat、 and depth。
2. `Approve plan and start meeting`。 Moderator #0。
3. `Advance assigned speaker`， or `/advance`。Moderator evaluates using `NEXT_SPEAKER` advisor seat；advisor seatcomplete through Moderatorevaluates and 。
4. User `Choose next speaker` or `/next`， directly schedule。advisor seatcan ， Moderator or User schedule 。
5. ， CONTINUEadvance ；User through ， End through 。
6. numbersseat、 facts、Open questions and candidatedecisions 。
7. Userdecided ， decisions confirmation、 、 ， or CONTINUE 。
8. `Conclude meeting`， or 。

 Ordinary `/advance` Rounds read-only verification tool。 、 、 Git Status or line 。

## 6. User interjection、 goal and decisions

User 。 `/` content directly for High-priority interjection use ， line conference for `Awaiting user`。

 ：

```text
 must ；data 。
```

conference ， 。 using ：

| | using |
| --- | --- |
| `/ask < > <question>` | propose question |
| `/answer < >` | Moderatorquestion； directly |
| `/goal < goal>` | Conference goal；conference UserStatus |
| `/focus <agendanumbers>` | agenda items ， `/focus 3` |
| `/status` | conferenceStatus |
| `/agenda` | agendaStatus and Phase conclusion |
| `/members` | numbersseat、 responsibilities and |
| `/questions` | Moderatorquestion、options、Status and User |
| `/help` | REPL |
| `/run` / `/continue` | `/advance` / `/resume` use |
| `/pause` / `/resume` | or CONTINUEconference |
| `/summary` | |
| `/setup` | or conferencedepth、numbersseat、 and rules |
| `/next` | using User |
| `/decision` | candidatedecisions |
| `/end` | conference |

candidatedecisions using ：

- `Confirm decision`： candidatedecisions for Userconfirmation。
- `Request more evidence`： evidence， advance。
- `Continue discussion`： ， 。
- `Reject decision`： User 。

## 7. tool using and line authorization 

 Ordinary conferenceRounds default 。 toolrequest and tool 。

if line 、 or ， `Run user-approved execution`， or ：

```text
/execute
```

System confirmation 。confirmation ， authorization ； END through 。

 、 task， “ confirmed configuration line ”。 line，Please using ， tool 。

## 8. Moderator advance

 Moderator schedule 、advance agenda items 、 using authorization tool， Userdecisions 。 default ， default use or tool 。

### configuration advance

 `Configure autopilot permissions`， or ：

```text
/autopilot
```

configuration ：

1. using `Up` / `Down` and Enter or using 。
2. line ：4、8、12 or 20。
3. using `Space` tool， using Enter 。
4. if the tool，confirmation authorization 。 default for 。

 authorized tool：

| tool | | |
| --- | --- | --- |
| `write_file` | use | using conference |
| `run_command` | line | line， |
| `fetch_http` | request HTTP/HTTPS | and |
| `browse_page` | | and |
| `web_search` | | for auditableevidence |

 tool ， line， has tool。 request tool，tool line using 。

### line and 

configurationcomplete ， `Run moderator autopilot`， or ：

```text
/auto run
```

 ：

```text
/auto on # using using 
/auto run # start advance
/auto off # 
```

 ； through configuration ：

- Moderator requestUserdecisions。
- User interjection、 、 goal or conference。
- Moderatorconfirmation all agenda items complete ENDconference。
- through configuration ， for `Paused`。
- or toolfailed ， schedule CONTINUE；User check。

 can User advance， 。 Ordinary rules、agenda、seat or advance Blocker or use start AI request， request using ； Ordinary and `/ask` request、 for ， Rounds。 output through providers ， for `Output limit` reason；System use agenda or decisions。

conference ，Moderator for ， quotes。 ， Discussion ； or failed for 。conference for seat output token ， using configuration output 。

## 9. tool and advance 

 ：

- `tool_authorization`： authorization or authorization and 。
- `tool_request`： request tool and 。
- `tool_result`：tool 。
- `autopilot_policy`：User using 、 or 。
- `autopilot_start`：Moderatorstart advance。
- `autopilot_pause`： User。
- `autopilot_limit`： advance through 。

 confirmation decisions、 line or ENDconference ， tool ，confirmation line and 。

## 10. and END

conferenceEND ：

```text
/summary
```

 。 goal、Status、Rounds、 facts、Open questions、decisions and action items。
 Moderatorcomplete finalconclusion（ ）， Participant seats and responsibilities、 agenda Status and Phase conclusion、Userquestion and ， and complete advisor 。 using line ， 、PageUp/PageDown、Home/End ， line words 。 `FACT:` or `DECISION:` ， ； 。

 Markdown ：

```text
/export release-plan.md
```

 pathmust conference path。 path ，System using `<conferenceID>-summary.md`。

complete ， `Conclude meeting`。System agenda、generatedcompleteStatus 。if the for complete，Please `End conference` or using `/end`。

## 11. using 

 risks or goal， using ：

1. using 、 Rounds Expert and Auditor candidate 。
2. candidatedecisions using `/decision` confirmation、 or 。
3. configuration ， 4 use start。
4. action items specific ， tool。
5. line checktool and ； 。
6. ， END confirmation facts、 risks 、action items and unresolved questions as 。

 AI ， goal、 authorization and finaldecided User 。
