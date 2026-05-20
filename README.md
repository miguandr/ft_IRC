# ft_IRC

An IRC server built from scratch in C++ as part of the 42 Berlin curriculum.

A fully functional IRC server written in C++98, built from scratch as part of the 42 Berlin core curriculum (rank 5). Handles multiple simultaneous client connections using a poll-based event loop. Clients connect with any standard IRC client and interact through channels, private messages, and operator commands.

---

## What it is

`ft_IRC` implements the server side of the IRC protocol (Internet Relay Chat). Real IRC clients like `irssi`, `WeeChat`, or `netcat` can connect to it and interact through standard IRC commands. The server handles multiple simultaneous connections using a single `poll()` event loop — no forking, no threads.

Built by [Miguel Andrade](https://github.com/miguandr) and [Daniela Torretta](https://github.com/dtorretta).

---

## Build

```bash
make        # compiles into ./ircserv
make clean  # removes object files
make fclean # removes objects + binary
make re     # full rebuild
```

Requires `g++-14` on macOS (auto-detected) or any C++98-compatible compiler on Linux.

---

## Run

```bash
./ircserv <port> <password>
```

Example:

```bash
./ircserv 6667 mypassword
```

- `port` must be in the range 1024–65535
- `password` cannot be empty
- Handles `SIGINT` (Ctrl+C) and `SIGTERM` for graceful shutdown

---

## Connect a client

**irssi:**

```bash
irssi
/connect localhost 6667 mypassword
```

**WeeChat:**

```bash
/server add local localhost/6667 -password=mypassword
/connect local
```

**netcat (best for evaluation — shows raw protocol):**

```bash
nc localhost 6667
PASS mypassword
NICK alice
USER alice 0 * :Alice
```

---

## Supported Commands

### Registration (required before anything else)

| Command | Description |
|---------|-------------|
| `PASS <password>` | Authenticate with the server password. Must be sent first. |
| `NICK <nickname>` | Set or change your nickname. Must start with a letter; allows `-_[]\^{}`. |
| `USER <username> 0 * :<realname>` | Complete registration. Required after `NICK`. |
| `QUIT [:<reason>]` | Disconnect from the server. Notifies all joined channels. |

Registration order: `PASS` → `NICK` → `USER`. All three are required before any channel commands are available.

### Channel Commands

| Command | Description |
|---------|-------------|
| `JOIN <#channel>[,<#channel2>] [key]` | Join one or more channels. Creates the channel if it doesn't exist. The first user to join becomes the operator. Supports comma-separated list and optional key. |
| `PART <#channel>[,<#channel2>] [:<reason>]` | Leave one or more channels. |
| `PRIVMSG <target> :<message>` | Send a message to a channel (`#channel`) or directly to a user (`nickname`). Supports comma-separated targets. |
| `TOPIC <#channel> [:<new topic>]` | View or set the topic. Without a message, shows the current topic. Setting requires operator privileges if mode `+t` is active. |
| `INVITE <nickname> <#channel>` | Invite a user to a channel. Required if channel is in invite-only mode (`+i`). Only operators can invite to `+i` channels. |
| `KICK <#channel>[,<#channel2>] <nickname> [:<reason>]` | Remove a user from a channel. Operator-only. |
| `MODE <#channel> [<+/-modes> [params]]` | View or change channel modes. Without mode flags, shows current active modes. Operator-only for modifications. |

---

## Channel Modes

Modes are set with `MODE #channel +<flags> [params]` and unset with `-<flags>`.

| Mode | Flag | Description |
|------|------|-------------|
| Invite-only | `+i` / `-i` | Only users who have been `INVITE`d can join the channel. |
| Topic restricted | `+t` / `-t` | Only channel operators can change the topic. |
| Key (password) | `+k <password>` / `-k <password>` | Requires a password to join. |
| Operator | `+o <nick>` / `-o <nick>` | Grant or revoke operator status for a user. Operators are shown with `@` in the member list. |
| User limit | `+l <number>` / `-l` | Set a maximum number of users in the channel. |

Multiple modes can be combined in a single command: `MODE #general +itk secretpass`

---

## Architecture

```
main.cpp
└── Server (sources/core/Server.cpp)
    ├── init()          — creates and binds the listening socket
    ├── execute()       — main poll() event loop
    ├── NewClient()     — accepts incoming connections
    ├── NewData()       — reads and buffers incoming data per client
    └── parser()        — routes commands to handlers via function pointer maps

sources/core/
├── Client.cpp  — per-connection state (fd, nick, username, buffer, auth flags)
└── Channel.cpp — channel state (members, admins, modes, topic, password)

sources/registration/
├── PassCommand.cpp  — PASS
├── NickCommand.cpp  — NICK
└── UserCommand.cpp  — USER

sources/commands/
├── JoinCommand.cpp     — JOIN
├── PartCommand.cpp     — PART
├── PrivmsgCommand.cpp  — PRIVMSG
├── TopicCommand.cpp    — TOPIC
├── InviteCommand.cpp   — INVITE
├── KickCommand.cpp     — KICK
├── ModeCommand.cpp     — MODE
└── QuitCommand.cpp     — QUIT

sources/utils/utils.cpp — signal handler, send/response helpers, socket cleanup

includes/utils/messages.hpp — all IRC response and error macros (RFC numeric codes)
```

### How the poll loop works

The server uses a single `poll()` call to monitor all file descriptors — both the listening socket and all connected client sockets. No threads, no blocking calls.

1. A new connection arrives on the listening socket → `NewClient()` accepts it, creates a `Client` object, and adds the fd to the poll list.
2. Data arrives on a client socket → `NewData()` reads into a per-client buffer. Because TCP is a stream protocol, messages can arrive in fragments. The buffer accumulates data until a `\r\n` delimiter is found.
3. Complete commands are split and passed one by one to `parser()`, which normalizes the command name to uppercase, then dispatches it through one of two `std::map<string, CommandHandler>` function pointer maps — one for registration commands, one for channel commands.
4. Channel commands require full registration (`PASS` + `NICK` + `USER`) before they're accessible.

### Client authentication states

```
Connected → (PASS) → passRegistered → (NICK + USER) → logedIn (fully registered)
```

### Channel member model

Each `Channel` holds two separate vectors: `_clients` (regular members) and `_admins` (operators). The first user to create a channel is automatically placed in `_admins`. Operators appear with `@` in the names list. Promotion/demotion via `MODE +o/-o` moves users between the two vectors atomically.

---

## Contribution split

**Daniela Torretta** — server core (`Server.cpp`, `Client.cpp`), the `poll()` event loop, connection handshake, client lifecycle, parser scaffolding, and the registration commands (`PASS`, `NICK`, `USER`).

**Miguel Andrade** — channel architecture (`Channel.cpp`), all channel commands (`JOIN`, `PART`, `PRIVMSG`, `TOPIC`, `INVITE`, `KICK`, `MODE`, `QUIT`), channel modes and permissions system, `messages.hpp` (all IRC numeric response codes), and overall integration.

---

## Files that should not be in this repo

Before making this public, run:

```bash
# Remove tracked files that shouldn't be here
git rm --cached .DS_Store
git rm --cached .vscode/settings.json
git rm --cached bircd.tar.gz
git rm --cached read.txt
git rm --cached IRC_architecture.txt
git commit -m "chore: remove system and dev files from tracking"
```

Then add a `.gitignore`:

```
.DS_Store
.vscode/
*.tar.gz
*.tar
*.zip
obj/
ircserv
read.txt
IRC_architecture.txt
```

