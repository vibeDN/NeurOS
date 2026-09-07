# Shared memory: one brain across every Claude

Claude Code (local, files) and Claude on claude.ai / the app (server-side, no
API) keep separate memory - there is no supported way to sync the two. Instead
we give every Claude **one shared memory they all use via a tool**, hosted on
an always-on Cloudflare Worker.

```
  claude.ai  ───(custom connector)──────────────┐
                                                 ▼
  Claude Code (desktop) ──(settings.json)──▶  neuros-memory Worker  ◀── KV
                                                 ▲   (memory_* tools,
  NeurOS device: claude ──▶ on-device cache ─────┘    browsable UI, [[links]])
                            (offline read-through /
                             write-behind to the Worker)
```

## The Worker  (repo: `~/coding/neuros-memory-worker`)

**URL:** `https://neuros-memory.fokus2082.workers.dev`

- `POST /mcp` - MCP streamable HTTP: `memory_list` / `memory_search` /
  `memory_read` / `memory_write` / `memory_delete`
- `GET /?k=<token>` - browsable index of every entry
- `GET /m/<name>?k=<token>` - one entry, `[[name]]` cross-links resolved
- `GET /raw/<name>?k=<token>` - plain text

Storage: Workers KV (`neuros-memory`, keys `mem:<name>`). Auth: the `MEM_TOKEN`
secret - Bearer header for `/mcp`, `?k=` on the browse pages. Deploy with
`./deploy.sh` (CF API directly, no wrangler). CF account `3db2a1a0…`,
subdomain `fokus2082`.

## Wiring the clients

### claude.ai
Settings → Connectors → **Add custom connector**
- URL: `https://neuros-memory.fokus2082.workers.dev/mcp`
- Authentication: **Bearer token** = the `MEM_TOKEN`

### Claude Code on the desktop
`~/.claude/settings.json`:
```json
"mcpServers": {
  "neuros-memory": {
    "type": "http",
    "url": "https://neuros-memory.fokus2082.workers.dev/mcp",
    "headers": { "Authorization": "Bearer <MEM_TOKEN>" }
  }
}
```
(already added on this machine)

### NeurOS device
`neuros-memory-mcp.service` runs `neuros-memory-mcp` as a **cache** in front of
the Worker: `MEMORY_MCP_URL` (in `/etc/neuros/memory.conf`) + the token
(`/etc/neuros/mcp-token`, baked dev-only by `post-build.sh`). The agent's
`/home/claude/.mcp.json` points `claude` at `http://127.0.0.1:8790/mcp`.
Reads pass through and are mirrored to `/home/claude/memory`; writes hit the
cache and are pushed (queued to `.pending` while offline, flushed on
reconnect). `neuros-mcp-url` prints the endpoint + token.

## System-prompt nudge

The NeurOS memory prompt already covers it. For the desktop, add to
`~/.claude/CLAUDE.md`:

> Shared long-term memory is available via the `memory_*` tools (also used by
> Claude on claude.ai). Call `memory_list` at the start of a task; when you
> learn a durable fact about the user or their projects, `memory_write` it -
> one fact per entry, short `--- name / summary / updated ---` frontmatter,
> cross-link with `[[other-name]]`.

## Rotate the token

```sh
NEW=$(head -c 24 /dev/urandom | base64 | tr -d '/+=')
curl -XPUT -H "Authorization: Bearer $(cat ~/.config/neuros/cf-token)" \
  "https://api.cloudflare.com/client/v4/accounts/3db2a1a07df6f273b7cd229d230d56bd/workers/scripts/neuros-memory/secrets" \
  -d "{\"name\":\"MEM_TOKEN\",\"text\":\"$NEW\",\"type\":\"secret_text\"}"
# then update: claude.ai connector, ~/.claude/settings.json, /etc/neuros/mcp-token
```

## `neuros-memsync` (optional, secondary)

`neuros-memsync` still git-syncs `/home/claude/memory` with `MEM_REMOTE` if set
- a versioned backup of the memory beyond KV. Not required now that the Worker
is canonical.
