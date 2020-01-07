#include <janet.h>
#include "http_parser.h"

#define HTTPSERVER_IMPL
#include "httpserver.h"

JanetTable *request_table;
JanetTable *headers;
Janet prev_header_name;
JanetFunction *handler;
http_parser_settings settings;
struct http_server_s* server;

void send_http_response(http_request_t* request, Janet res) {
  switch (janet_type(res)) {
      case JANET_TABLE:
      case JANET_STRUCT:
        {
            const JanetKV *kvs;
            int32_t kvlen, kvcap;
            janet_dictionary_view(res, &kvs, &kvlen, &kvcap);

            Janet status = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("status"));
            Janet headers = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("headers"));
            Janet body = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("body"));

            int code;
            if (janet_checktype(status, JANET_NIL))
                code = 200;
            else if (janet_checkint(status))
                code = janet_unwrap_integer(status);
            else
                break;

            const JanetKV *headerkvs;
            int32_t headerlen, headercap;
            if (janet_checktype(headers, JANET_NIL)) {
                headerkvs = NULL;
                headerlen = 0;
                headercap = 0;
            } else if (!janet_dictionary_view(headers, &headerkvs, &headerlen, &headercap)) {
                break;
            }

            const uint8_t *body_bytes;
            int32_t body_len;
            if (janet_checktype(body, JANET_NIL)) {
              body_bytes = NULL;
              body_len = 0;
            } else if (!janet_bytes_view(body, &body_bytes, &body_len)) {
              break;
            }

            struct http_response_s* response = http_response_init();

            http_response_status(response, code);

            for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
                    kv;
                    kv = janet_dictionary_next(headerkvs, headercap, kv)) {
                const uint8_t *name = janet_to_string(kv->key);
                const uint8_t *value = janet_to_string(kv->value);
                http_response_header(response, (const char *)name, (const char *)value);
            }

            if (body_len > 0) {
              http_response_body(response, (const char *)body_bytes, body_len);
            }

            http_respond(request, response);
        }
        break;
      default:
        error_response(request, 500, "Internal server error");
        break;
  }
}

int message_begin_cb(struct http_parser *parser) {
  (void)parser;

  return 0;
}

int header_field_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  prev_header_name = janet_wrap_string(janet_string((uint8_t *)p, len));

  return 0;
}

int header_value_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  janet_table_put(headers, prev_header_name, janet_wrap_string(janet_string((uint8_t *)p, len)));
  return 0;
}

int status_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;
  (void)p;
  (void)len;

  return 0;
}

int url_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  janet_table_put(request_table, janet_ckeywordv("uri"), janet_wrap_string(janet_string((const uint8_t *)p, len)));

  return 0;
}

int body_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  janet_table_put(request_table, janet_ckeywordv("body"), janet_wrap_string(janet_string((const uint8_t *)p, len)));

  return 0;
}

int headers_complete_cb(http_parser *parser) {

  janet_table_put(request_table, janet_ckeywordv("method"), janet_wrap_string(janet_cstring(http_method_str(parser->method))));
  janet_table_put(request_table, janet_ckeywordv("headers"), janet_wrap_table(headers));

  return 0;
}

int message_complete_cb(struct http_parser *parser) {
  (void)parser;

  return 0;
}

void handle_request(struct http_request_s* request) {
  request_table = janet_table(5);
  headers = janet_table(20);
  int nparsed = 0;
  http_parser parser;
  http_parser_init(&parser, HTTP_REQUEST);
  nparsed = http_parser_execute(&parser, &settings, request->buf, request->bytes);

  Janet jarg[1];
  jarg[0] = janet_wrap_table(request_table);
  Janet response;
  JanetFiber *fiber = janet_fiber(handler, 64, 1, jarg);
  JanetSignal signal = janet_continue(fiber, jarg[0], &response);
  if(signal != JANET_SIGNAL_OK) {
    janet_stacktrace(fiber, response);
  } else {
    send_http_response(request, response);
  }
}

Janet cfun_start_server(int32_t argc, Janet *argv) {
  janet_arity(argc, 2, 3);

  JanetFunction *janet_handler = janet_getfunction(argv, 0);
  int32_t port = janet_getinteger(argv, 1);

  settings.on_message_begin     = message_begin_cb;
  settings.on_header_field      = header_field_cb;
  settings.on_header_value      = header_value_cb;
  settings.on_status            = status_cb;
  settings.on_url               = url_cb;
  settings.on_body              = body_cb;
  settings.on_headers_complete  = headers_complete_cb;
  settings.on_message_complete  = message_complete_cb;

  handler = janet_handler;

  server = http_server_init(port, handle_request);
  http_server_listen_poll(server);

  if (!server) {
    janet_panicf("failed to intialize server\n");
  }

  return janet_wrap_nil();
}

Janet cfun_poll_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  (void)argv;

  http_server_poll(server);

  return janet_wrap_nil();
}

Janet cfun_stop_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  (void)argv;

  return janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"start-server", cfun_start_server, NULL},
    {"poll-server", cfun_poll_server, NULL},
    {"stop-server", cfun_stop_server, NULL},
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

