# Shared memory: one brain across the phone, the desktop, and claude.ai

Claude Code (local, files) and claude.ai / the Claude app (server-side, no API)
keep separate memory. There is no supported way to sync them. What we *can* do
is give both a **shared memory they both use via a tool** - the MCP server here.

```
  claude.ai  ──(custom connector, over a tunnel)──┐
                                                  ├─▶  neuros-memory-mcp  ──▶  memory dir
  Claude Code (phone / desktop) ──(--mcp-config)──┘        (memory_* tools)      (*.md files)
                                                                                     │
                                                        neuros-memsync (git) ────────┘
                                                        keeps the phone + desktop dirs equal
```

## Pieces

| component | where | what |
|-----------|-------|------|
| `neuros-memory-mcp` | phone + desktop | MCP server (streamable HTTP, :8790). Tools: `memory_list` / `memory_search` / `memory_read` / `memory_write` / `memory_delete`. Bearer token at `/etc/neuros/mcp-token` (phone) or `~/.config/neuros/mcp-token` (desktop). |
| `neuros-memory-tunnel.service` | the canonical host | `cloudflared` quick tunnel exposing :8790. **Blocked behind the dev machine's NL VPN** - so the **phone** (RU mobile) is the canonical host. |
| `neuros-mcp-url` | phone | prints the tunnel URL + token for the claude.ai connector |
| `neuros-memsync` | phone + desktop | `git` sync of the memory dir with `MEM_REMOTE` (a private repo), union-merge, around sessions |

## Setup

### 1. Phone = canonical host

```sh
systemctl enable --now neuros-memory-mcp.service neuros-memory-tunnel.service
neuros-mcp-url          # -> URL: https://xxxx.trycloudflare.com/mcp   Token: ....
```
(the quick-tunnel URL changes on every restart - re-run `neuros-mcp-url` and
update the connector, or set up a *named* Cloudflare tunnel for a stable domain.)

### 2. claude.ai connector

claude.ai → Settings → Connectors → **Add custom connector**
- URL: the `.../mcp` URL from `neuros-mcp-url`
- Authentication: **Bearer token**, the token from `neuros-mcp-url`

Then in a chat: it can call `memory_list` / `memory_write` etc. Ask it to
"check your shared memory" at the start and "save that to shared memory" when
it learns something durable.

### 3. Desktop Claude Code

`~/.claude/settings.json`:
```json
{
  "mcpServers": {
    "neuros-memory": {
      "type": "http",
      "url": "https://xxxx.trycloudflare.com/mcp",
      "headers": { "Authorization": "Bearer <token from neuros-mcp-url>" }
    }
  }
}
```
Use the phone's tunnel URL (works through the VPN - it's plain HTTPS out). When
the phone is offline, point it at the local desktop server
(`http://127.0.0.1:8790/mcp`, `~/.config/neuros/mcp-token`) and let
`neuros-memsync` reconcile.

### 4. Phone Claude Code

The phone's agent already reads/writes `/home/claude/memory/` directly (the
memory system prompt), so no `--mcp-config` is needed there - it shares the same
files the MCP server serves.

### 5. git sync (optional but recommended)

Create a private `neuros-memory` repo, add a write deploy key on each machine,
then `/etc/neuros/memsync.conf`:
```sh
MEM_REMOTE="git@github.com:you/neuros-memory.git"
```
`neuros-memsync sync` before/after sessions (wire `push` into the Claude Code
Stop hook). Now the phone dir, the desktop dir and the git history all match.

## System prompt nudge

Add to `~/.claude/CLAUDE.md` (desktop) and it's already implied by the NeurOS
memory prompt (phone):

> You have a shared long-term memory via the `memory_*` tools (also used by
> Claude on claude.ai). Call `memory_list` at the start of a task. When you
> learn a durable fact about the user or their projects, `memory_write` it -
> one fact per file, short frontmatter (`name` / `summary` / `updated`).
