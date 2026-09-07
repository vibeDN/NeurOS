// neuros-memory-mcp - a tiny MCP server (streamable HTTP) exposing NeurOS's
// agent memory directory as read/write tools, so both Claude Code on the device
// and Claude on claude.ai (via a custom connector over a tunnel) work off the
// same memory. Stdlib only.
package main

import (
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
	"time"
)

var (
	root    = envOr("NEUROS_MEMORY_DIR", "/home/claude/memory")
	addr    = envOr("NEUROS_MCP_ADDR", "127.0.0.1:8790")
	token   = loadToken()
	version = "0.1.0"
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

var tools = []toolDef{
	{"memory_list", "List every memory file with its one-line summary and last-updated date. Call this at the start of a task.",
		obj(nil)},
	{"memory_search", "Case-insensitive substring search across all memory files. Returns the matching files with the matching lines.",
		obj(map[string]any{"query": str("text to search for")}, "query")},
	{"memory_read", "Return the full contents of one memory file.",
		obj(map[string]any{"path": str("path relative to the memory root, e.g. you/name.md")}, "path")},
	{"memory_write", "Create or overwrite a memory file (one durable fact per file). Keep a short '--- name / summary / updated ---' frontmatter then bullet facts. Path is relative to the memory root and must end in .md; group with a subdir if you like (you/, topics/, area/).",
		obj(map[string]any{"path": str("relative path, e.g. topics/food.md"), "content": str("full file contents")}, "path", "content")},
	{"memory_delete", "Delete a memory file that is no longer true or relevant.",
		obj(map[string]any{"path": str("relative path")}, "path")},
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

func callTool(name string, raw json.RawMessage) (string, error) {
	var a struct {
		Path, Content, Query string
	}
	_ = json.Unmarshal(raw, &a)

	switch name {
	case "memory_list":
		files := mdFiles()
		if len(files) == 0 {
			return "(memory is empty)", nil
		}
		var b strings.Builder
		for _, f := range files {
			rel, _ := filepath.Rel(root, f)
			data, _ := os.ReadFile(f)
			sum, upd := "", ""
			if m := fmRe.FindSubmatch(data); m != nil {
				fm := string(m[1])
				sum = fmField(fm, "summary")
				if sum == "" {
					sum = fmField(fm, "description")
				}
				upd = fmField(fm, "updated")
				if upd == "" {
					upd = fmField(fm, "modified")
				}
			}
			fmt.Fprintf(&b, "%s", rel)
			if upd != "" {
				fmt.Fprintf(&b, "  (updated %s)", upd)
			}
			if sum != "" {
				fmt.Fprintf(&b, " - %s", sum)
			}
			b.WriteByte('\n')
		}
		return b.String(), nil

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
		if !strings.HasSuffix(a.Path, ".md") {
			return "", fmt.Errorf("path must end in .md")
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
		return "wrote " + a.Path, nil

	case "memory_delete":
		full, err := safePath(a.Path)
		if err != nil {
			return "", err
		}
		if err := os.Remove(full); err != nil {
			return "", fmt.Errorf("not found: %s", a.Path)
		}
		return "deleted " + a.Path, nil
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
	log.Printf("neuros-memory-mcp %s: %s/mcp  (memory: %s, auth: %v)", version, addr, root, token != "")
	log.Fatal(srv.ListenAndServe())
}
