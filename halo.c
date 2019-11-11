#include <janet.h>
#include <stdio.h>
#include <string.h>
#include "civetweb.h"

struct mg_context *ctx;
JanetFunction * janet_handler;

static Janet build_http_request(const struct mg_request_info *request_info) {
    JanetTable *payload = janet_table(5);

    janet_table_put(payload, janet_ckeywordv("uri"), janet_wrap_string(request_info->local_uri));
    janet_table_put(payload, janet_ckeywordv("query-string"), janet_wrap_string(request_info->query_string));
    janet_table_put(payload, janet_ckeywordv("method"), janet_wrap_string(request_info->request_method));
    janet_table_put(payload, janet_ckeywordv("connection"), janet_wrap_abstract(request_info->user_data));

    /* Add headers */
    JanetTable *headers = janet_table(request_info->num_headers);
    for (int i = 0; i < request_info->num_headers; i++) {
        janet_table_put(headers,
                janet_wrap_string(request_info->http_headers[i].name),
                janet_wrap_string(request_info->http_headers[i].value));
    }

    janet_table_put(payload, janet_ckeywordv("headers"), janet_wrap_table(headers));

    return janet_wrap_table(payload);
}

// This function will be called by civetweb on every new request.
static int begin_request_handler(struct mg_connection *conn) {
    const struct mg_request_info *request_info = mg_get_request_info(conn);

    janet_init();

    Janet jarg[1];

    jarg[0] = build_http_request(request_info);

    JanetFiber * fiber = janet_current_fiber();
    Janet out;
    JanetSignal signal = janet_pcall(janet_handler, 1, jarg, &out, &fiber);

    if (signal != JANET_SIGNAL_OK) {
      janet_stacktrace(fiber, out);
    } else if(janet_checktype(out, JANET_STRUCT) || janet_checktype(out, JANET_TABLE)) {
      const JanetKV *kvs;
      int32_t kvlen, kvcap;
      janet_dictionary_view(out, &kvs, &kvlen, &kvcap);

      Janet body = janet_dictionary_get(kvs, kvcap, janet_cstringv("body"));
      Janet status = janet_dictionary_get(kvs, kvcap, janet_cstringv("status"));
      Janet headers = janet_dictionary_get(kvs, kvcap, janet_cstringv("headers"));

      int code;
      if (janet_checktype(status, JANET_NIL)) {
        code = 200;
      } else if (janet_checkint(status)) {
        code = janet_unwrap_integer(status);
      } else {
        code = 200;
      }

      const JanetKV *headerkvs;
      int32_t headerlen, headercap;
      if (janet_checktype(headers, JANET_NIL)) {
          headerkvs = NULL;
          headerlen = 0;
          headercap = 0;
      } else if (!janet_dictionary_view(headers, &headerkvs, &headerlen, &headercap)) {
      }

      const uint8_t *bodybytes;
      int32_t bodylen;
      if (janet_checktype(body, JANET_NIL)) {
        bodybytes = NULL;
        bodylen = 0;
      } else if (!janet_bytes_view(body, &bodybytes, &bodylen)) {
      }

      mg_printf(conn, "HTTP/1.1 %d %s\r\n", code, mg_get_response_code_text(conn, code));

      for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
              kv;
              kv = janet_dictionary_next(headerkvs, headercap, kv)) {
          const uint8_t *name = janet_to_string(kv->key);
          const uint8_t *value = janet_to_string(kv->value);
          mg_printf(conn, "%s: %s\r\n", (const char *)name, (const char *)value);
      }

      if (bodylen) {
          mg_printf(conn, "Content-Length: %d\r\n", bodylen);
      }

      mg_printf(conn, "\r\n%.*s", bodylen, (const char *)bodybytes);

    } else {

      mg_printf(conn,
                "HTTP/1.1 500 Internal Server Error\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: %d\r\n"        // Always set Content-Length
                "\r\n",
                0);

    }

    janet_deinit();

    // Returning non-zero tells civetweb that our function has replied to
    // the client, and civetweb should not send client any more data.
    return 1;
}


static Janet cfun_start(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 2);

  struct mg_callbacks callbacks;
  janet_handler = janet_getfunction(argv, 0);
  const uint8_t *interface = janet_getstring(argv, 1);

  // List of options. Last element must be NULL.
  const char *options[] = {"listening_ports", interface, NULL};

  // Prepare callbacks structure. We have only one callback, the rest are NULL.
  memset(&callbacks, 0, sizeof(callbacks));
  callbacks.begin_request = begin_request_handler;

  // Start the web server.
  ctx = mg_start(&callbacks, NULL, options);

  // Wait until user hits a key. Server is running in separate thread.
  // Navigating to http://localhost:8080 will invoke begin_request_handler().
  getchar();

  mg_stop(ctx);

  return janet_wrap_nil();
}

static void cfun_stop(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  // Stop the server.
  mg_stop(ctx);
}

static const JanetReg cfuns[] = {
    {"start", cfun_start, NULL},
    {"stop", cfun_stop, NULL},
    {NULL, NULL, NULL}
};

extern const unsigned char *halo_lib_embed;
extern size_t halo_lib_embed_size;

JANET_MODULE_ENTRY(JanetTable *env) {
    janet_cfuns(env, "halo", cfuns);

    janet_dobytes(env,
            halo_lib_embed,
            halo_lib_embed_size,
            "halo_lib.janet",
            NULL);
}
