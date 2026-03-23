#include "rpc.h"
#include "../log/log.h"

#include <stdio.h>
#include <string.h>

#define TAG "core.proto"

/* ── Parse ────────────────────────────────────────────────────────────────── */

int rpc_parse(const char *line, RpcRequest *req) {
    memset(req, 0, sizeof(*req));
    req->id = -1;

    json_tokener *tok = json_tokener_new();
    req->root = json_tokener_parse_ex(tok, line, (int)strlen(line));
    json_tokener_free(tok);

    if (!req->root || !json_object_is_type(req->root, json_type_object)) {
        log_warn(TAG, "JSON parse error: %s", line);
        if (req->root) json_object_put(req->root);
        req->root = NULL;
        return -1;
    }

    json_object *jid, *jmethod, *jparams;

    if (json_object_object_get_ex(req->root, "id", &jid))
        req->id = json_object_get_int(jid);

    if (!json_object_object_get_ex(req->root, "method", &jmethod)) {
        log_warn(TAG, "RPC message missing 'method'");
        json_object_put(req->root);
        req->root = NULL;
        return -1;
    }
    snprintf(req->method, sizeof(req->method), "%s",
             json_object_get_string(jmethod));

    req->params = NULL;
    if (json_object_object_get_ex(req->root, "params", &jparams))
        req->params = jparams; /* borrowed – owner is req->root */

    return 0;
}

void rpc_request_free(RpcRequest *req) {
    if (req && req->root) {
        json_object_put(req->root);
        req->root   = NULL;
        req->params = NULL;
    }
}

/* ── Build responses ──────────────────────────────────────────────────────── */

int rpc_build_result(char *buf, size_t buf_sz, int id,
                     const char *result_json) {
    if (!result_json || result_json[0] == '\0') result_json = "{}";
    return snprintf(buf, buf_sz, "{\"id\":%d,\"result\":%s}", id, result_json);
}

int rpc_build_error(char *buf, size_t buf_sz, int id, const char *message) {
    return snprintf(buf, buf_sz, "{\"id\":%d,\"error\":\"%s\"}", id, message);
}

/* ── Param accessors ──────────────────────────────────────────────────────── */

int rpc_param_int(json_object *params, const char *key, int def) {
    if (!params) return def;
    json_object *v;
    if (!json_object_object_get_ex(params, key, &v)) return def;
    return json_object_get_int(v);
}

double rpc_param_double(json_object *params, const char *key, double def) {
    if (!params) return def;
    json_object *v;
    if (!json_object_object_get_ex(params, key, &v)) return def;
    return json_object_get_double(v);
}

const char *rpc_param_str(json_object *params, const char *key) {
    if (!params) return NULL;
    json_object *v;
    if (!json_object_object_get_ex(params, key, &v)) return NULL;
    return json_object_get_string(v);
}
