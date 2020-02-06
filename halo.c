#include <janet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_parser.h"
#include "sds.h"
#include <uv.h>

#define DEFAULT_BACKLOG 128

JanetTable *request_table;
JanetTable *headers;
Janet prev_header_name;
JanetFunction *handler;
http_parser_settings settings;
uv_loop_t *loop;
struct sockaddr_in addr;

typedef struct {
    uv_write_t req;
    uv_buf_t buf;
} write_req_t;

sds http_response(Janet janet_response) {
  sds response = sdsnew("");

  switch (janet_type(janet_response)) {
      case JANET_TABLE:
      case JANET_STRUCT:
        {
            const JanetKV *kvs;
            int32_t kvlen, kvcap;
            janet_dictionary_view(janet_response, &kvs, &kvlen, &kvcap);

            /* TODO static files? */
            // const uint8_t *file_path;
            // struct stat s;
            // int err;
            //Janet janet_filepath = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("file"));

            /* Serve a generic HTTP response */
            Janet status = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("status"));
            Janet headers = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("headers"));
            Janet body = janet_dictionary_get(kvs, kvcap, janet_ckeywordv("body"));

            int code;
            if (janet_checktype(status, JANET_NIL)) {
                code = 200;
            } else if (janet_checkint(status)) {
                code = janet_unwrap_integer(status);
            } else {
                break;
            }

            response = sdscatprintf(response, "HTTP/1.1 %d %s\r\n", code, http_status_str(code));

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

            for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
                    kv;
                    kv = janet_dictionary_next(headerkvs, headercap, kv)) {
                const uint8_t *name = janet_to_string(kv->key);
                const uint8_t *value = janet_to_string(kv->value);
                response = sdscatprintf(response, "%s: %s\r\n", (const char *)name, (const char *)value);
            }

            if (body_len > 0) {
              int length = snprintf(NULL, 0, "%d", body_len);
              char str[length];

              sprintf(str, "%d", body_len);

              response = sdscatprintf(response, "Content-Length: %s\r\n", str);
              response = sdscatprintf(response, "\r\n%s", (char *)body_bytes);
            }
        }
        break;
      default:
        response = sdscat(response, "HTTP 500 Internal server Error\r\nContent-Type: text/plain\r\nContent-Length: 22\r\n\r\nInternal Server Error\n");
        break;
  }

  return response;
}

void free_write_req(uv_write_t *req) {
    write_req_t *wr = (write_req_t*) req;
    free(wr->buf.base);
    free(wr);
}

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
    buf->base = (char*) malloc(suggested_size);
    buf->len = suggested_size;
}

void on_close(uv_handle_t* handle) {
    free(handle);
}

void echo_write(uv_write_t *req, int status) {
    if (status) {
        fprintf(stderr, "Write error %s\n", uv_strerror(status));
    }
    uv_close((uv_handle_t *)req->handle, on_close);
    free_write_req(req);
}

void echo_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
        write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
        req->buf = uv_buf_init(buf->base, nread);

        request_table = janet_table(5);
        headers = janet_table(20);
        int nparsed = 0;
        http_parser parser;
        http_parser_init(&parser, HTTP_REQUEST);
        nparsed = http_parser_execute(&parser, &settings, buf->base, nread);

        Janet jarg[1];
        jarg[0] = janet_wrap_table(request_table);
        Janet janet_response;
        JanetFiber *fiber = janet_fiber(handler, 64, 1, jarg);
        JanetFiber *janet_vm_fiber = janet_current_fiber();
        if (!janet_vm_fiber->env) {
             janet_vm_fiber->env = janet_table(0);
        }
        fiber->env = janet_vm_fiber->env;
        JanetSignal signal = janet_continue(fiber, jarg[0], &janet_response);
        if(signal != JANET_SIGNAL_OK) {
             janet_stacktrace(fiber, janet_response);

             if (nread != UV_EOF)
                 fprintf(stderr, "Read error %s\n", uv_err_name(nread));
             uv_close((uv_handle_t*) client, on_close);

             return;
        }

        sds response_string = http_response(janet_response);

        uv_buf_t res;
        res.base = response_string;
        res.len = sdslen(response_string);

        uv_write((uv_write_t*) req, client, &res, 1, echo_write);
        sdsfree(response_string);
        return;
    }

    if (nread < 0) {
        if (nread != UV_EOF)
            fprintf(stderr, "Read error %s\n", uv_err_name(nread));
        uv_close((uv_handle_t*) client, on_close);
    }

    free(buf->base);
}

void on_new_connection(uv_stream_t *server, int status) {
    if (status < 0) {
        fprintf(stderr, "New connection error %s\n", uv_strerror(status));
        // error!
        return;
    }

    uv_tcp_t *client = (uv_tcp_t*) malloc(sizeof(uv_tcp_t));
    uv_tcp_init(loop, client);
    if (uv_accept(server, (uv_stream_t*) client) == 0) {
        uv_read_start((uv_stream_t*) client, alloc_buffer, echo_read);
    }
    else {
        uv_close((uv_handle_t*) client, on_close);
    }
}

/****************************************************************
 *********************** Parse Headers **************************
 ****************************************************************/

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

/****************************************************************
 *********************** Janet Section **************************
 ****************************************************************/

Janet cfun_start_server(int32_t argc, Janet *argv) {
    janet_arity(argc, 2, 3);

    handler = janet_getfunction(argv, 0);
    int32_t port = janet_getinteger(argv, 1);
    const uint8_t *ip_address = janet_optstring(argv, argc, 2, NULL);

    settings.on_message_begin     = message_begin_cb;
    settings.on_header_field      = header_field_cb;
    settings.on_header_value      = header_value_cb;
    settings.on_status            = status_cb;
    settings.on_url               = url_cb;
    settings.on_body              = body_cb;
    settings.on_headers_complete  = headers_complete_cb;
    settings.on_message_complete  = message_complete_cb;

    loop = uv_default_loop();

    uv_tcp_t server;
    uv_tcp_init(loop, &server);

    uv_ip4_addr((const char *)ip_address, (int)port, &addr);

    uv_tcp_bind(&server, (const struct sockaddr*)&addr, 0);
    int r = uv_listen((uv_stream_t*) &server, DEFAULT_BACKLOG, on_new_connection);
    if (r) {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        janet_panicf("Listen error %s\n", uv_strerror(r));
        return janet_wrap_nil();
    }

    uv_run(loop, UV_RUN_DEFAULT);

    return janet_wrap_nil();
}

Janet cfun_stop_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 0);
  (void)argv;

  return janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"start-server", cfun_start_server, NULL},
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

