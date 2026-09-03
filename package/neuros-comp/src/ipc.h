/*
 * NeurOS compositor control socket.
 *
 * A line-oriented Unix socket at $XDG_RUNTIME_DIR/neuros-comp.sock. neuros-agentd
 * (or `neuros-ctl`) writes one command per line:
 *
 *   agent <name>            set the top-pane block text
 *   status <state>          set the bottom-pane block text (Working/Thinking/...)
 *   colors <#rrggbb> <#rrggbb>   wallpaper gradient stops (top, bottom)
 *
 * MIT.
 */
#ifndef NG_IPC_H
#define NG_IPC_H

struct cg_server;

struct ng_ipc *ng_ipc_create(struct cg_server *server);
void ng_ipc_destroy(struct ng_ipc *ipc);

#endif
