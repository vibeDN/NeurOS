// neuros-memory-mcp - a tiny MCP server (streamable HTTP) for the shared agent
// memory. On the NeurOS device it runs as an *offline cache* in front of the
// canonical neuros-memory Cloudflare Worker (set NEUROS_MCP_UPSTREAM): reads go
// through and are mirrored to a local dir; writes hit the local dir immediately
// and are pushed to the Worker (queued while offline, flushed on reconnect).
// With no upstream it is just a local memory-dir server. Stdlib only.
package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

var (
	root     = envOr("NEUROS_MEMORY_DIR", "/home/claude/memory")
	addr     = envOr("NEUROS_MCP_ADDR", "127.0.0.1:8790")
	token    = loadToken()
	upstream = strings.TrimRight(os.Getenv("NEUROS_MCP_UPSTREAM"), "/") // e.g. https://x.workers.dev/mcp
	upTok    = strings.TrimSpace(os.Getenv("NEUROS_MCP_UPSTREAM_TOKEN"))
	version  = "0.2.0"
	pending  = filepath.Join(envOr("NEUROS_MEMORY_DIR", "/home/claude/memory"), ".pending")
	pmu      sync.Mutex
)

func envOr(k, d string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return d
}

func loadToken() string {
	if v := os.Getenv("NEUROS_MCP_TOKEN"); v != "" {
		return strings.TrimSpace(v)
	}
	if b, err := os.ReadFile(envOr("NEUROS_MCP_TOKEN_FILE", "/etc/neuros/mcp-token")); err == nil {
		return strings.TrimSpace(string(b))
	}
	return "" // empty = no auth (local-only use)
}

// ---- JSON-RPC 2.0 ----------------------------------------------------------

type rpcReq struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id,omitempty"`
	Method  string          `json:"method"`
	Params  json.RawMessage `json:"params,omitempty"`
}
type rpcErr struct {
	Code    int    `json:"code"`
	Message string `json:"message"`
}
type rpcResp struct {
	JSONRPC string          `json:"jsonrpc"`
	ID      json.RawMessage `json:"id"`
	Result  any             `json:"result,omitempty"`
	Error   *rpcErr         `json:"error,omitempty"`
}

func result(id json.RawMessage, r any) rpcResp {
	return rpcResp{JSONRPC: "2.0", ID: id, Result: r}
}
func fail(id json.RawMessage, code int, msg string) rpcResp {
	return rpcResp{JSONRPC: "2.0", ID: id, Error: &rpcErr{code, msg}}
}

// ---- MCP -----------------------------------------------------------------

type toolDef struct {
	Name        string `json:"name"`
	Description string `json:"description"`
	InputSchema any    `json:"inputSchema"`
}

func obj(props map[string]any, required ...string) map[string]any {
	if props == nil {
		props = map[string]any{}
	}
	m := map[string]any{"type": "object", "properties": props}
	if len(required) > 0 {
		m["required"] = required
	}
	return m
}
func str(desc string) map[string]any { return map[string]any{"type": "string", "description": desc} }

const layout = "Memory is organised in four folders, one .md file per entry:\n" +
	"  you/<x>     - stable facts about the user: name, routine, people, long-term preferences\n" +
	"  topics/<x>  - recurring interests/habits, one file per subject (food, work, hobbies...)\n" +
	"  area/<x>    - an ongoing project or situation with a clear end state (a trip, a repair, a goal)\n" +
	"  blog/<x>    - the neuroblog persona's journal (imported from the old Claudeproject vault):\n" +
	"                Telegram chat history, running gags, people, tech quirks. Sub-folders\n" +
	"                blog/journal/<date>, blog/people/<id>, blog/threads/<x>; start from blog/index"

var folders = []string{"you", "topics", "area", "blog"}

var tools = []toolDef{
	{"memory_list", "List every memory entry, grouped by folder, with its one-line summary and last-updated date. Call this at the start of a task.\n\n" + layout,
		obj(nil)},
	{"memory_search", "Case-insensitive substring search across all memory entries; returns the matching entries and lines.",
		obj(map[string]any{"query": str("text to search for")}, "query")},
	{"memory_read", "Return the full contents of one memory entry.",
		obj(map[string]any{"path": str("entry name, e.g. you/name or topics/food")}, "path")},
	{"memory_write", "Create or overwrite one memory entry. The path MUST be under you/, topics/, area/ or blog/ - you choose which based on the layout below. Add to an existing entry rather than making near-duplicates (memory_read it first). Only write facts that would still matter in a month.\n\n" + layout + "\n\nEach file: a short '--- name / summary / updated ---' frontmatter, then bullet facts. Cross-link with [[you/name]].",
		obj(map[string]any{"path": str("you/<x> | topics/<x> | area/<x> | blog/<x>"), "content": str("full entry text")}, "path", "content")},
	{"memory_delete", "Delete a memory entry that is no longer true or relevant.",
		obj(map[string]any{"path": str("entry name")}, "path")},
}

func handleRPC(req rpcReq) (rpcResp, bool) {
	notification := len(req.ID) == 0
	switch req.Method {
	case "initialize":
		return result(req.ID, map[string]any{
			"protocolVersion": "2025-06-18",
			"capabilities":    map[string]any{"tools": map[string]any{}},
			"serverInfo":      map[string]any{"name": "neuros-memory", "version": version},
			"instructions":    "Shared long-term memory for this user, read and written by both Claude Code on the NeurOS device and Claude on claude.ai. Check memory_list at the start of a task; write a durable fact with memory_write when you learn one.",
		}), true
	case "notifications/initialized", "notifications/cancelled":
		return rpcResp{}, false
	case "ping":
		return result(req.ID, map[string]any{}), true
	case "tools/list":
		return result(req.ID, map[string]any{"tools": tools}), true
	case "tools/call":
		var p struct {
			Name      string          `json:"name"`
			Arguments json.RawMessage `json:"arguments"`
		}
		_ = json.Unmarshal(req.Params, &p)
		text, err := callTool(p.Name, p.Arguments)
		if err != nil {
			return result(req.ID, map[string]any{
				"content": []any{map[string]any{"type": "text", "text": "error: " + err.Error()}},
				"isError": true,
			}), true
		}
		return result(req.ID, map[string]any{
			"content": []any{map[string]any{"type": "text", "text": text}},
		}), true
	default:
		if notification {
			return rpcResp{}, false
		}
		return fail(req.ID, -32601, "method not found: "+req.Method), true
	}
}

// ---- tools --------------------------------------------------------------

var fmRe = regexp.MustCompile(`(?s)^---\s*\n(.*?)\n---\s*\n`)

func safePath(rel string) (string, error) {
	rel = strings.TrimPrefix(filepath.Clean("/"+rel), "/")
	if rel == "" || strings.Contains(rel, "..") {
		return "", fmt.Errorf("bad path")
	}
	full := filepath.Join(root, rel)
	if !strings.HasPrefix(full+string(filepath.Separator), filepath.Clean(root)+string(filepath.Separator)) {
		return "", fmt.Errorf("path escapes memory root")
	}
	return full, nil
}

func mdFiles() []string {
	var out []string
	filepath.Walk(root, func(p string, fi os.FileInfo, err error) error {
		if err == nil && !fi.IsDir() && strings.HasSuffix(p, ".md") {
			out = append(out, p)
		}
		return nil
	})
	sort.Strings(out)
	return out
}

func fmField(body, key string) string {
	for _, ln := range strings.Split(body, "\n") {
		if strings.HasPrefix(strings.ToLower(strings.TrimSpace(ln)), key+":") {
			return strings.TrimSpace(ln[strings.Index(ln, ":")+1:])
		}
	}
	return ""
}

// --- upstream (canonical Worker) proxy + offline queue ------------------

func upCall(name string, raw json.RawMessage) (string, error) {
	body, _ := json.Marshal(map[string]any{
		"jsonrpc": "2.0", "id": 1, "method": "tools/call",
		"params": map[string]any{"name": name, "arguments": json.RawMessage(raw)},
	})
	req, _ := http.NewRequest("POST", upstream, bytes.NewReader(body))
	req.Header.Set("Content-Type", "application/json")
	if upTok != "" {
		req.Header.Set("Authorization", "Bearer "+upTok)
	}
	resp, err := (&http.Client{Timeout: 8 * time.Second}).Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()
	if resp.StatusCode/100 != 2 {
		return "", fmt.Errorf("upstream %d", resp.StatusCode)
	}
	var rr struct {
		Result struct {
			Content []struct{ Text string }
			IsError bool
		}
	}
	if err := json.NewDecoder(resp.Body).Decode(&rr); err != nil || len(rr.Result.Content) == 0 {
		return "", fmt.Errorf("bad upstream reply")
	}
	if rr.Result.IsError {
		return "", fmt.Errorf("%s", rr.Result.Content[0].Text)
	}
	return rr.Result.Content[0].Text, nil
}

func queue(name string, raw json.RawMessage) {
	pmu.Lock()
	defer pmu.Unlock()
	f, err := os.OpenFile(pending, os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0o644)
	if err != nil {
		return
	}
	defer f.Close()
	line, _ := json.Marshal(map[string]any{"name": name, "args": json.RawMessage(raw)})
	f.Write(append(line, '\n'))
}

func flush() {
	pmu.Lock()
	data, err := os.ReadFile(pending)
	if err != nil || len(data) == 0 {
		pmu.Unlock()
		return
	}
	os.Remove(pending)
	pmu.Unlock()
	for _, ln := range strings.Split(strings.TrimSpace(string(data)), "\n") {
		var q struct {
			Name string
			Args json.RawMessage
		}
		if json.Unmarshal([]byte(ln), &q) != nil {
			continue
		}
		if _, err := upCall(q.Name, q.Args); err != nil {
			queue(q.Name, q.Args) // still offline - requeue
		}
	}
}

// mirror a path/content pair straight into the local dir (cache fill)
func mirror(path, content string) {
	if !strings.HasSuffix(path, ".md") {
		path += ".md"
	}
	full, err := safePath(path)
	if err != nil {
		return
	}
	os.MkdirAll(filepath.Dir(full), 0o755)
	os.WriteFile(full, []byte(content), 0o644)
}

func callTool(name string, raw json.RawMessage) (string, error) {
	if upstream == "" {
		return callToolLocal(name, raw)
	}
	switch name {
	case "memory_list", "memory_search":
		if t, err := upCall(name, raw); err == nil {
			return t, nil
		}
		return callToolLocal(name, raw) // offline
	case "memory_read":
		if t, err := upCall(name, raw); err == nil {
			var a struct{ Path string }
			_ = json.Unmarshal(raw, &a)
			mirror(a.Path, t)
			return t, nil
		}
		return callToolLocal(name, raw)
	case "memory_write", "memory_delete":
		lt, lerr := callToolLocal(name, raw) // local first, always
		if _, err := upCall(name, raw); err != nil {
			queue(name, raw)
			if lerr == nil {
				return lt + " (queued for sync)", nil
			}
		}
		return lt, lerr
	}
	return callToolLocal(name, raw)
}

func callToolLocal(name string, raw json.RawMessage) (string, error) {
	var a struct {
		Path, Content, Query string
	}
	_ = json.Unmarshal(raw, &a)
	// canonical entry names have no .md (matches the Worker); files on disk do
	if a.Path != "" && !strings.HasSuffix(a.Path, ".md") {
		a.Path += ".md"
	}

	switch name {
	case "memory_list":
		files := mdFiles()
		if len(files) == 0 {
			return "(memory is empty - files go under you/, topics/, area/ or blog/)", nil
		}
		grp := map[string][]string{}
		for _, f := range files {
			rel, _ := filepath.Rel(root, f)
			rel = strings.TrimSuffix(filepath.ToSlash(rel), ".md")
			data, _ := os.ReadFile(f)
			sum, upd := "", ""
			if m := fmRe.FindSubmatch(data); m != nil {
				fm := string(m[1])
				if sum = fmField(fm, "summary"); sum == "" {
					sum = fmField(fm, "description")
				}
				if upd = fmField(fm, "updated"); upd == "" {
					upd = fmField(fm, "modified")
				}
			}
			row := "  " + rel + ".md"
			if upd != "" {
				row += "  (" + upd + ")"
			}
			if sum != "" {
				row += " - " + sum
			}
			top := strings.SplitN(rel, "/", 2)[0]
			grp[top] = append(grp[top], row)
		}
		var b strings.Builder
		for _, k := range []string{"you", "topics", "area", "blog"} {
			if len(grp[k]) > 0 {
				fmt.Fprintf(&b, "%s/\n%s\n", k, strings.Join(grp[k], "\n"))
				delete(grp, k)
			}
		}
		for k, rows := range grp {
			fmt.Fprintf(&b, "%s/\n%s\n", k, strings.Join(rows, "\n"))
		}
		return strings.TrimRight(b.String(), "\n"), nil

	case "memory_search":
		if a.Query == "" {
			return "", fmt.Errorf("query required")
		}
		q := strings.ToLower(a.Query)
		var b strings.Builder
		for _, f := range mdFiles() {
			data, _ := os.ReadFile(f)
			rel, _ := filepath.Rel(root, f)
			var hits []string
			for _, ln := range strings.Split(string(data), "\n") {
				if strings.Contains(strings.ToLower(ln), q) {
					hits = append(hits, "  "+strings.TrimSpace(ln))
				}
			}
			if len(hits) > 0 {
				fmt.Fprintf(&b, "%s:\n%s\n", rel, strings.Join(hits, "\n"))
			}
		}
		if b.Len() == 0 {
			return "no matches", nil
		}
		return b.String(), nil

	case "memory_read":
		full, err := safePath(a.Path)
		if err != nil {
			return "", err
		}
		data, err := os.ReadFile(full)
		if err != nil {
			return "", fmt.Errorf("not found: %s", a.Path)
		}
		return string(data), nil

	case "memory_write":
		top := strings.SplitN(strings.TrimPrefix(a.Path, "/"), "/", 2)[0]
		ok := false
		for _, f := range folders {
			if top == f {
				ok = true
			}
		}
		if !ok {
			return "", fmt.Errorf("entry must be under you/, topics/, area/ or blog/  (e.g. topics/food)")
		}
		full, err := safePath(a.Path)
		if err != nil {
			return "", err
		}
		if a.Content == "" {
			return "", fmt.Errorf("content required")
		}
		if err := os.MkdirAll(filepath.Dir(full), 0o755); err != nil {
			return "", err
		}
		if err := os.WriteFile(full, []byte(a.Content), 0o644); err != nil {
			return "", err
		}
		return "wrote " + strings.TrimSuffix(a.Path, ".md"), nil

	case "memory_delete":
		full, err := safePath(a.Path)
		if err != nil {
			return "", err
		}
		if err := os.Remove(full); err != nil {
			return "", fmt.Errorf("not found: %s", a.Path)
		}
		return "deleted " + strings.TrimSuffix(a.Path, ".md"), nil
	}
	return "", fmt.Errorf("unknown tool: %s", name)
}

// ---- HTTP --------------------------------------------------------------

func mcpHandler(w http.ResponseWriter, r *http.Request) {
	if token != "" {
		got := strings.TrimPrefix(r.Header.Get("Authorization"), "Bearer ")
		if strings.TrimSpace(got) != token {
			http.Error(w, "unauthorized", http.StatusUnauthorized)
			return
		}
	}
	switch r.Method {
	case http.MethodGet:
		// clients may open an SSE stream for server->client messages; we have none.
		w.Header().Set("Content-Type", "text/event-stream")
		w.WriteHeader(http.StatusOK)
		return
	case http.MethodDelete:
		w.WriteHeader(http.StatusOK)
		return
	case http.MethodPost:
	default:
		http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
		return
	}

	body, _ := io.ReadAll(io.LimitReader(r.Body, 4<<20))
	// support a JSON-RPC batch or a single message
	var single rpcReq
	var batch []rpcReq
	if json.Unmarshal(body, &batch) == nil && len(batch) > 0 {
	} else if json.Unmarshal(body, &single) == nil {
		batch = []rpcReq{single}
	} else {
		http.Error(w, "bad json", http.StatusBadRequest)
		return
	}

	var out []rpcResp
	for _, req := range batch {
		if resp, send := handleRPC(req); send {
			out = append(out, resp)
		}
	}

	w.Header().Set("Content-Type", "application/json")
	if len(out) == 0 {
		w.WriteHeader(http.StatusAccepted)
		return
	}
	if len(out) == 1 {
		json.NewEncoder(w).Encode(out[0])
		return
	}
	json.NewEncoder(w).Encode(out)
}

func main() {
	log.SetFlags(0)
	if _, err := os.Stat(root); err != nil {
		log.Printf("neuros-memory-mcp: memory dir %s not present yet (will be created on write)", root)
	}
	mux := http.NewServeMux()
	mux.HandleFunc("/mcp", mcpHandler)
	mux.HandleFunc("/healthz", func(w http.ResponseWriter, r *http.Request) { fmt.Fprintln(w, "ok") })
	srv := &http.Server{Addr: addr, Handler: mux, ReadHeaderTimeout: 5 * time.Second}
	if upstream != "" {
		log.Printf("neuros-memory-mcp %s: %s/mcp  (cache for %s)", version, addr, upstream)
		go func() {
			for {
				flush()
				time.Sleep(30 * time.Second)
			}
		}()
	} else {
		log.Printf("neuros-memory-mcp %s: %s/mcp  (memory: %s, auth: %v)", version, addr, root, token != "")
	}
	log.Fatal(srv.ListenAndServe())
}
