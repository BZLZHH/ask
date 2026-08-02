# ask 

`ask` line AI ， ： 、 ， and AI Conference。 OpenAI 、Anthropic and Gemini。

## 1. 

 C++20、CMake、libcurl、JsonCpp、SQLite、ncursesw、libedit；Linux if line ， Bubblewrap。

Debian or Ubuntu：

```sh
sudo apt install build-essential cmake ninja-build pkg-config \
 libcurl4-openssl-dev libjsoncpp-dev libsqlite3-dev libncursesw5-dev libedit-dev bubblewrap
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

 using `./build/ask`。 directly using `ask`。

## 2. configuration providers

 configuration ：

```sh
./build/ask --config
```

 `Providers` using or providers， use 、API 、 default and 。 ， use configuration 。 DeepSeek providers using ：

```sh
export DEEPSEEK_API_KEY='your-api-key'
```

complete ， General Settings default providers and default 。 using ，Enter or confirmation，Esc ， `Save changes`。

## 3. 

 line：

```sh
./build/ask " "
```

 using ：

```sh
./build/ask --provider deepseek --model deepseek-v4-flash " CMake configuration"
```

 ， REPL `Conversation entry` 。 REPL CONTINUE directly question；Ctrl-C generated request 。

 using Ordinary REPL ：

```text
!help 
!config configuration through 
!do <task> for using tool
!ask <task> using 
!model providers and 
!compact 
?COMMAND lineUser shell output 
!q QUIT
```

 。 ：

```sh
./build/ask resume
```

 QUIT 10 directly line `./build/ask` 。

## 4. Ask and Do 

 default Ask ， 、 、 Git information and SystemStatus。 。

 task genuinely 、 line or ， Do ：

```sh
./build/ask --do "check failed reason， line "
```

Do ： ， Linux Bubblewrap line， User confirmation。 can Ask request or Do authorization ； 。

## 5. and 

 TTY default line QUIT， ：

```sh
git diff | ./build/ask " ， risks question"
./build/ask --no-repl --no-stream "generated information"
```

 using JSON：

```sh
result=$(printf '%s' ' ' | ./build/ask --json)
printf '%s\n' "$result" | jq -r '.text'
```

`--json` output ，stdout output JSON ；tool and use stderr。

## 6. AI Conference

 task 、 risks or ， using conference ：

```sh
./build/ask conference " for line ， path"
```

 conference `Preparing plan`。Moderator using generated conference ， seat 、 、 、providers and 。User `Review meeting plan` confirmation or start 。

conference using ：agenda and Status、 、 。 and Enter can complete using ； using 、 Moderatorquestion and line REPL。

conference REPL using ：

```text
/help conference REPL 
/status Status 
/agenda Agenda and phase conclusions
/members seat、 responsibilities and 
/questions Moderatorquestion、options and 
/advance or /run advance schedule 
/pause /resume or CONTINUE
/answer <content> Moderatorquestion
/next User 
/setup seat、 、rules or depth
/autopilot configuration advance and tool authorization 
/summary Meeting Minutes
/export report.md Markdown 
/end conference 
```

Moderator propose 、 or question， 。 can using ， Ordinary directly 。advisor seat directly User， Moderator 。

 conference：

```sh
./build/ask conference resume
```

 conference [AI_CONFERENCE_GUIDE.md](AI_CONFERENCE_GUIDE.md)， and [AI_CONFERENCE.md](AI_CONFERENCE.md)。

## 7. question

** provider unavailable or failed**

 line `./build/ask --config`，confirmationproviders using 、API and default 。if the using ，confirmation shell `export` 。

** tool using or output **

 configuration check Capability profile。 or 。

** line**

confirmation using `--do` or authorization Do ；Linux Bubblewrap。 line， through directly line。

**conference CONTINUE**

checkconferenceStatus：`Awaiting user` Moderator or decisions；`Paused` `/resume`； advance using `/auto run`。 `Error`、`Output limit` and tool specific reason。

**data **

configuration default `$XDG_CONFIG_HOME/ask/config.json` or `~/.config/ask/config.json`； 、conference and Status XDG data 。 conference `ask conference resume` 。
