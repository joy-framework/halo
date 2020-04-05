#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <janet.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "http_parser.h"
#include "sandbird.h"

JanetTable *request_table;
JanetTable *headers;
Janet prev_header_name;
JanetFunction *handler;
http_parser_settings settings;
sb_Options opt;
sb_Server *server;
int server_running = 1;

static void sig_handler(int signo) {
  if (signo == SIGINT) {
    server_running = 0;
  }
}


void send_http_response(sb_Event *e, Janet res) {
  switch (janet_type(res)) {
      case JANET_TABLE:
      case JANET_STRUCT:
        {
            const JanetKV *kvs;
            int32_t kvlen, kvcap;
            janet_dictionary_view(res, &kvs, &kvlen, &kvcap);

            /* check for static files */
            const uint8_t *file_path;
            struct stat s;
            int err;
            Janet janet_filepath = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("file"));

            if(janet_checktype(janet_filepath, JANET_STRING)) {
              file_path = janet_unwrap_string(janet_filepath);
              /* Get file info */
              err = stat((const char *)file_path, &s);

              /* Does file exist? */
              if (err == -1) {
                sb_send_status(e->stream, 404, "Not found");
                return;
              }

              // TODO Directories?

              /* Handle file */
              err = sb_send_file(e->stream, (const char *)file_path);

              if (err) {
                break;
              } else {
                return;
              }
            }

            /* Serve a generic HTTP response */
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

            const char *code_text = http_status_str(code);
            sb_send_status(e->stream, code, code_text);

            for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
                    kv;
                    kv = janet_dictionary_next(headerkvs, headercap, kv)) {

              const uint8_t *name = janet_to_string(kv->key);

              int32_t header_len;
               const Janet *header_items;
               if (janet_indexed_view(kv->value, &header_items, &header_len)) {
                 for (int32_t i = 0; i < header_len; i++) {
                   const uint8_t *value = janet_to_string(header_items[i]);
                   sb_send_header(e->stream, (const char *)name, (const char *)value);
                 }
               } else {
                 const uint8_t *value = janet_to_string(kv->value);
                 sb_send_header(e->stream, (const char *)name, (const char *)value);
               }
            }

            if (body_len > 0) {
              int length = snprintf(NULL, 0, "%d", body_len);
              char str[length];

              sprintf(str, "%d", body_len);

              sb_send_header(e->stream, "Content-Length", str);
              sb_writef(e->stream, (const char *)body_bytes);
            }
        }
        break;
      default:
        sb_send_status(e->stream, 500, "Internal server error");
        sb_send_header(e->stream, "Content-Type", "text/plain");
        sb_writef(e->stream, "Internal Server Error");
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

  Janet header = janet_table_get(headers, prev_header_name);
  Janet value = janet_wrap_string(janet_string((uint8_t *)p, len));

  switch (janet_type(header)) {
    case JANET_NIL:
      janet_table_put(headers, prev_header_name, value);
      break;
    case JANET_ARRAY:
      janet_array_push(janet_unwrap_array(header), value);
      break;
    default: {
      Janet newHeader[2] = { header, value };
      janet_table_put(headers, prev_header_name, janet_wrap_array(janet_array_n(newHeader, 2)));
      break;
    }
  }

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

static int event_handler(sb_Event *e) {
  if (e->type == SB_EV_REQUEST) {

    request_table = janet_table(5);
    headers = janet_table(20);
    int nparsed = 0;
    http_parser parser;
    http_parser_init(&parser, HTTP_REQUEST);
    nparsed = http_parser_execute(&parser, &settings, e->stream->recv_buf.s, e->stream->recv_buf.len);

    // TODO while loop to parse all the streams
    // keep calling e->stream->next until NULL
    // multipart
    //if(nparsed != e->stream->recv_buf.len) {
      // parse next stream
    //}

    Janet jarg[1];
    jarg[0] = janet_wrap_table(request_table);
    Janet response;
    JanetFiber *fiber = janet_fiber(handler, 64, 1, jarg);
    JanetFiber *janet_vm_fiber = janet_current_fiber();
    if (!janet_vm_fiber->env) {
        janet_vm_fiber->env = janet_table(0);
    }
    fiber->env = janet_vm_fiber->env;
    JanetSignal signal = janet_continue(fiber, jarg[0], &response);
    if(signal != JANET_SIGNAL_OK) {
      janet_stacktrace(fiber, response);
      return SB_RES_CLOSE;
    }

    send_http_response(e, response);
  }

  return SB_RES_OK;
}

Janet cfun_start_server(int32_t argc, Janet *argv) {
  janet_arity(argc, 2, 3);

  JanetFunction *janet_handler = janet_getfunction(argv, 0);
  const uint8_t *port = janet_getstring(argv, 1);
  const uint8_t *ip_address = janet_optstring(argv, argc, 2, NULL);

  settings.on_message_begin     = message_begin_cb;
  settings.on_header_field      = header_field_cb;
  settings.on_header_value      = header_value_cb;
  settings.on_status            = status_cb;
  settings.on_url               = url_cb;
  settings.on_body              = body_cb;
  settings.on_headers_complete  = headers_complete_cb;
  settings.on_message_complete  = message_complete_cb;

  memset(&opt, 0, sizeof(opt));

  opt.port = (char *)port;
  if(ip_address != NULL) {
    opt.host = (char *)ip_address;
  }
  opt.handler = event_handler;
  handler = janet_handler;

  server = sb_new_server(&opt);

  if (!server) {
    janet_panicf("failed to intialize server\n");
  }

  return janet_wrap_nil();
}

Janet cfun_poll_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 1);

  int32_t timeout = janet_getinteger(argv, 0);

  sb_poll_server(server, timeout);

  return janet_wrap_nil();
}

Janet cfun_server_running(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  (void)argv;

  return janet_wrap_boolean(server_running);
}


Janet cfun_stop_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  (void)argv;

  sb_close_server(server);

  return janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"start-server", cfun_start_server, NULL},
    {"poll-server", cfun_poll_server, NULL},
    {"stop-server", cfun_stop_server, NULL},
    {"server-running?", cfun_server_running, NULL},
    {NULL, NULL, NULL}
};

extern const unsigned char *halo_lib_embed;
extern size_t halo_lib_embed_size;

JANET_MODULE_ENTRY(JanetTable *env) {
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
      printf("\ncan't catch SIGINT\n");
    }

    janet_cfuns(env, "halo", cfuns);

    janet_dobytes(env,
            halo_lib_embed,
            halo_lib_embed_size,
            "halo_lib.janet",
            NULL);
}

