#ifndef DRONE_RPC_H
#define DRONE_RPC_H

#include <stddef.h>

/*
 * rpc.h — minimal JSON-RPC helpers
 *
 * Request  (client → server): {"id":N,"method":"string","params":{...}}\n
 * Response (server → client): {"id":N,"result":{...}}\n
 * Error    (server → client): {"id":N,"error":"message"}\n
 *
 * Uses json-c (libjson-c-dev) for parsing.
 */

#include <json-c/json.h>

/* Parsed inbound request */
typedef struct {
    int          id;
    char         method[64];
    json_object *params;    /* borrowed reference — do NOT put it */
    json_object *root;      /* owner — call rpc_request_free() when done */
} RpcRequest;

/*
 * Parse one newline-stripped JSON line into req.
 * Returns 0 on success, -1 on parse error.
 */
int rpc_parse(const char *line, RpcRequest *req);

/* Release the request object. */
void rpc_request_free(RpcRequest *req);

/*
 * Build a success response string into buf (NUL-terminated, no trailing \n).
 * result_json may be NULL for an empty result ({}).
 * Returns number of bytes written (excluding NUL), or -1 on overflow.
 */
int rpc_build_result(char *buf, size_t buf_sz, int id,
                     const char *result_json);

/*
 * Build an error response string into buf.
 */
int rpc_build_error(char *buf, size_t buf_sz, int id, const char *message);

/* ── Convenience param accessors (return 0 / default on missing key) ─────── */
int         rpc_param_int   (json_object *params, const char *key, int def);
double      rpc_param_double(json_object *params, const char *key, double def);
const char *rpc_param_str   (json_object *params, const char *key);

#endif /* DRONE_RPC_H */
