#include <janet.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/event.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "sds.h"
#include "http_parser.h"


sds build_http_response(sds response_string, Janet res) {
  switch (janet_type(res)) {
      case JANET_TABLE:
      case JANET_STRUCT:
        {
            const JanetKV *kvs;
            int32_t kvlen, kvcap;
            janet_dictionary_view(res, &kvs, &kvlen, &kvcap);

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

            const uint8_t *bodybytes;
            int32_t bodylen;
            if (janet_checktype(body, JANET_NIL)) {
              bodybytes = NULL;
              bodylen = 0;
            } else if (!janet_bytes_view(body, &bodybytes, &bodylen)) {
              break;
            }

            const char *code_text = http_status_str(code);
            response_string = sdscatprintf(response_string, "HTTP/1.1 %d %s\n", code, code_text);

            for (const JanetKV *kv = janet_dictionary_next(headerkvs, headercap, NULL);
                    kv;
                    kv = janet_dictionary_next(headerkvs, headercap, kv)) {
                const uint8_t *name = janet_to_string(kv->key);
                const uint8_t *value = janet_to_string(kv->value);
                response_string = sdscatprintf(response_string, "%s: %s\r\n", (const char *)name, (const char *)value);
            }

            if (bodylen) {
              response_string = sdscatprintf(response_string, "Content-Length: %d\r\n", bodylen);
            }

            response_string = sdscatprintf(response_string, "\r\n%.*s", bodylen, (const char *)bodybytes);
        }
        break;
      default:
        response_string = sdscat(response_string, "HTTP/1.1 500 Internal Server Error\nContent-Type: text/plain;\n\nInternal Server Error");
        break;
  }

  return response_string;
}

JanetTable *payload;
JanetTable *headers;
Janet prev_header_name;

int message_begin_cb(struct http_parser *parser) {
  (void)parser;

  return 0;
}

int header_field_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  prev_header_name = janet_cstringv(janet_string((uint8_t *)p, len));

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

  janet_table_put(payload, janet_ckeywordv("uri"), janet_wrap_string(janet_string((const uint8_t *)p, len)));

  return 0;
}

int body_cb(struct http_parser *parser, const char *p, unsigned long len) {
  (void)parser;

  janet_table_put(payload, janet_ckeywordv("body"), janet_wrap_string(janet_string((const uint8_t *)p, len)));

  return 0;
}

int headers_complete_cb(http_parser *parser) {
  sds http_version = sdscatprintf(sdsempty(), "%hu.%hu", parser->http_major, parser->http_minor);

  janet_table_put(payload, janet_ckeywordv("method"), janet_wrap_string(janet_cstring(http_method_str(parser->method))));
  janet_table_put(payload, janet_ckeywordv("http-version"), janet_wrap_string(janet_cstring(http_version)));

  sdsfree(http_version);

  return 0;
}

int message_complete_cb(struct http_parser *parser) {
  (void)parser;

  return 0;
}

int server(JanetFunction *handler, int32_t port, const uint8_t *ip_address) {
  struct sockaddr_in server_address;
  int server_fd;

  http_parser parser;

  // Init http parser callbacks
  http_parser_settings settings;
  settings.on_message_begin     = message_begin_cb;
  settings.on_header_field      = header_field_cb;
  settings.on_header_value      = header_value_cb;
  settings.on_status            = status_cb;
  settings.on_url               = url_cb;
  settings.on_body              = body_cb;
  settings.on_headers_complete  = headers_complete_cb;
  settings.on_message_complete  = message_complete_cb;

  if ((server_fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    fprintf(stderr, "Problem acquiring socket: %s\n", strerror(errno));
    exit(1);
  }

  int opt_reuse_addr = 1;
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse_addr,
      sizeof(opt_reuse_addr));

  memset(&server_address, 0, sizeof(server_address));
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = inet_addr((char *)ip_address);
  server_address.sin_port = htons(port);

  if (bind(server_fd, (struct sockaddr *) &server_address,
        sizeof(server_address)) < 0) {
    fprintf(stderr, "Problem binding to socket: %s\n", strerror(errno));
    exit(1);
  }

  if (listen(server_fd, 1000) < 0) {
    fprintf(stderr, "Problem setting fd to listen: %s\n", strerror(errno));
    close(server_fd);
    exit(1);
  }

  const int RECEIVE_SIZE = 4096;
  char receive_buf[RECEIVE_SIZE];
  int received_bytes = 1;

  int kq = kqueue();
  struct kevent ev_set;
  struct kevent ev_list[32];
  int event_count, event_iter, client_fd;
  struct sockaddr_storage client_addr;
  socklen_t client_addr_len = sizeof(client_addr);

  EV_SET(&ev_set, server_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);

  if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0) {
    fprintf(stderr, "Problem setting up read filter on kqueue: %s\n",
        strerror(errno));
    close(server_fd);
    exit(1);
  }

  while (1) {
    if ((event_count = kevent(kq, NULL, 0, ev_list, 32, NULL)) < 1) {
      fprintf(stderr, "Problem asking for next kqueue event: %s\n",
          strerror(errno));
      close(server_fd);
      exit(1);
    }

    for (event_iter = 0; event_iter < event_count; event_iter++) {
      if (ev_list[event_iter].ident == server_fd) {
        if ((client_fd = accept(ev_list[event_iter].ident,
                (struct sockaddr *) &client_addr, &client_addr_len)) < 0) {
          fprintf(stderr, "Problem accepting new connection: %s\n",
              strerror(errno));
        }

        setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse_addr,
            sizeof(opt_reuse_addr));

        EV_SET(&ev_set, client_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);

        if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0) {
          fprintf(stderr, "Problem adding kevent listener for client: %s\n",
              strerror(errno));
        }
      } else {
        if (ev_list[event_iter].flags & EVFILT_READ) {

          received_bytes = recv(ev_list[event_iter].ident, receive_buf,
              sizeof(receive_buf), 0);

          if (received_bytes <= 0) {
            fprintf(stderr, "Error receiving bytes: %s\n", strerror(errno));
            close(ev_list[event_iter].ident);
            break;
          }

          payload = janet_table(4);
          headers = janet_table(50);
          int nparsed = 0;
          http_parser_init(&parser, HTTP_REQUEST);
          nparsed = http_parser_execute(&parser, &settings, receive_buf, received_bytes);

          // hack for safari
          if(janet_equals(janet_wrap_nil(), janet_table_get(payload, janet_ckeywordv("body")))) {
              received_bytes = recv(ev_list[event_iter].ident, receive_buf,
                  sizeof(receive_buf), 0);

              nparsed = http_parser_execute(&parser, &settings, receive_buf, received_bytes);
          }

          if (nparsed != received_bytes) {
            fprintf(stderr, "nparsed: %d\n", nparsed);
            fprintf(stderr, "received_bytes: %d\n", received_bytes);
            fprintf(stderr, "Error parsing http %s\n", http_errno_description(errno));
            close(ev_list[event_iter].ident);
            break;
          }

          Janet jarg[1];
          janet_table_put(payload, janet_ckeywordv("headers"), janet_wrap_table(headers));
          jarg[0] = janet_wrap_table(payload);
          Janet janet_response = janet_call(handler, 1, jarg);

          sds response = sdsempty();
          response = build_http_response(response, janet_response);
          int response_size = (int)sdslen(response);

          send(ev_list[event_iter].ident, response, response_size, 0);

          sdsfree(response);
          memset(&receive_buf, 0, sizeof(receive_buf));

          ev_list[event_iter].flags = ev_list[event_iter].flags ^ EV_EOF;
        }

        if (ev_list[event_iter].flags & EV_EOF) {
          EV_SET(&ev_set, ev_list[event_iter].ident, EVFILT_READ, EV_DELETE,
              0, 0, NULL);

          if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0) {
            fprintf(stderr, "Problem removing kevent for client: %s\n",
                strerror(errno));
            close(ev_list[event_iter].ident);
            exit(1);
          }

          close(ev_list[event_iter].ident);

        }
      }
    }
  }

  return 0;
}

Janet cfun_start_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  JanetFunction *handler = janet_getfunction(argv, 0);
  const int32_t port = janet_getinteger(argv, 1);
  const uint8_t *ip_address = janet_getstring(argv, 2);

  server(handler, port, ip_address);

  return janet_wrap_nil();
}

static const JanetReg cfuns[] = {
    {"start-server", cfun_start_server, NULL},
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

