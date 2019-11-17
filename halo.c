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
#include "picohttpparser.h"

// Shamelessly stolen from https://github.com/civetweb/civetweb
const char * response_code_text(int response_code) {
  /* See IANA HTTP status code assignment:
   * http://www.iana.org/assignments/http-status-codes/http-status-codes.xhtml
   */

  switch (response_code) {
    /* RFC2616 Section 10.1 - Informational 1xx */
    case 100:
      return "Continue"; /* RFC2616 Section 10.1.1 */
    case 101:
      return "Switching Protocols"; /* RFC2616 Section 10.1.2 */
    case 102:
      return "Processing"; /* RFC2518 Section 10.1 */

    /* RFC2616 Section 10.2 - Successful 2xx */
    case 200:
      return "OK"; /* RFC2616 Section 10.2.1 */
    case 201:
      return "Created"; /* RFC2616 Section 10.2.2 */
    case 202:
      return "Accepted"; /* RFC2616 Section 10.2.3 */
    case 203:
      return "Non-Authoritative Information"; /* RFC2616 Section 10.2.4 */
    case 204:
      return "No Content"; /* RFC2616 Section 10.2.5 */
    case 205:
      return "Reset Content"; /* RFC2616 Section 10.2.6 */
    case 206:
      return "Partial Content"; /* RFC2616 Section 10.2.7 */
    case 207:
      return "Multi-Status"; /* RFC2518 Section 10.2, RFC4918 Section 11.1 */
    case 208:
      return "Already Reported"; /* RFC5842 Section 7.1 */

    case 226:
      return "IM used"; /* RFC3229 Section 10.4.1 */

    /* RFC2616 Section 10.3 - Redirection 3xx */
    case 300:
      return "Multiple Choices"; /* RFC2616 Section 10.3.1 */
    case 301:
      return "Moved Permanently"; /* RFC2616 Section 10.3.2 */
    case 302:
      return "Found"; /* RFC2616 Section 10.3.3 */
    case 303:
      return "See Other"; /* RFC2616 Section 10.3.4 */
    case 304:
      return "Not Modified"; /* RFC2616 Section 10.3.5 */
    case 305:
      return "Use Proxy"; /* RFC2616 Section 10.3.6 */
    case 307:
      return "Temporary Redirect"; /* RFC2616 Section 10.3.8 */
    case 308:
      return "Permanent Redirect"; /* RFC7238 Section 3 */

    /* RFC2616 Section 10.4 - Client Error 4xx */
    case 400:
      return "Bad Request"; /* RFC2616 Section 10.4.1 */
    case 401:
      return "Unauthorized"; /* RFC2616 Section 10.4.2 */
    case 402:
      return "Payment Required"; /* RFC2616 Section 10.4.3 */
    case 403:
      return "Forbidden"; /* RFC2616 Section 10.4.4 */
    case 404:
      return "Not Found"; /* RFC2616 Section 10.4.5 */
    case 405:
      return "Method Not Allowed"; /* RFC2616 Section 10.4.6 */
    case 406:
      return "Not Acceptable"; /* RFC2616 Section 10.4.7 */
    case 407:
      return "Proxy Authentication Required"; /* RFC2616 Section 10.4.8 */
    case 408:
      return "Request Time-out"; /* RFC2616 Section 10.4.9 */
    case 409:
      return "Conflict"; /* RFC2616 Section 10.4.10 */
    case 410:
      return "Gone"; /* RFC2616 Section 10.4.11 */
    case 411:
      return "Length Required"; /* RFC2616 Section 10.4.12 */
    case 412:
      return "Precondition Failed"; /* RFC2616 Section 10.4.13 */
    case 413:
      return "Request Entity Too Large"; /* RFC2616 Section 10.4.14 */
    case 414:
      return "Request-URI Too Large"; /* RFC2616 Section 10.4.15 */
    case 415:
      return "Unsupported Media Type"; /* RFC2616 Section 10.4.16 */
    case 416:
      return "Requested range not satisfiable"; /* RFC2616 Section 10.4.17 */
    case 417:
      return "Expectation Failed"; /* RFC2616 Section 10.4.18 */

    case 421:
      return "Misdirected Request"; /* RFC7540 Section 9.1.2 */
    case 422:
      return "Unproccessable entity"; /* RFC2518 Section 10.3, RFC4918
                                     * Section 11.2 */
    case 423:
      return "Locked"; /* RFC2518 Section 10.4, RFC4918 Section 11.3 */
    case 424:
      return "Failed Dependency"; /* RFC2518 Section 10.5, RFC4918
                                 * Section 11.4 */

    case 426:
      return "Upgrade Required"; /* RFC 2817 Section 4 */

    case 428:
      return "Precondition Required"; /* RFC 6585, Section 3 */
    case 429:
      return "Too Many Requests"; /* RFC 6585, Section 4 */

    case 431:
      return "Request Header Fields Too Large"; /* RFC 6585, Section 5 */

    case 451:
      return "Unavailable For Legal Reasons"; /* draft-tbray-http-legally-restricted-status-05,
                                             * Section 3 */

    /* RFC2616 Section 10.5 - Server Error 5xx */
    case 500:
      return "Internal Server Error"; /* RFC2616 Section 10.5.1 */
    case 501:
      return "Not Implemented"; /* RFC2616 Section 10.5.2 */
    case 502:
      return "Bad Gateway"; /* RFC2616 Section 10.5.3 */
    case 503:
      return "Service Unavailable"; /* RFC2616 Section 10.5.4 */
    case 504:
      return "Gateway Time-out"; /* RFC2616 Section 10.5.5 */
    case 505:
      return "HTTP Version not supported"; /* RFC2616 Section 10.5.6 */
    case 506:
      return "Variant Also Negotiates"; /* RFC 2295, Section 8.1 */
    case 507:
      return "Insufficient Storage"; /* RFC2518 Section 10.6, RFC4918
                                    * Section 11.5 */
    case 508:
      return "Loop Detected"; /* RFC5842 Section 7.1 */

    case 510:
      return "Not Extended"; /* RFC 2774, Section 7 */
    case 511:
      return "Network Authentication Required"; /* RFC 6585, Section 6 */

    /* Other status codes, not shown in the IANA HTTP status code assignment.
     * E.g., "de facto" standards due to common use, ... */
    case 418:
      return "I am a teapot"; /* RFC2324 Section 2.3.2 */
    case 419:
      return "Authentication Timeout"; /* common use */
    case 420:
      return "Enhance Your Calm"; /* common use */
    case 440:
      return "Login Timeout"; /* common use */
    case 509:
      return "Bandwidth Limit Exceeded"; /* common use */

  default:
    /* This error code is unknown. This should not happen. */
    printf("Unknown HTTP response code: %d", response_code);

    /* Return at least a category according to RFC 2616 Section 10. */
    if (response_code >= 100 && response_code < 200) {
      /* Unknown informational status code */
      return "Information";
    }

    if (response_code >= 200 && response_code < 300) {
      /* Unknown success code */
      return "Success";
    }

    if (response_code >= 300 && response_code < 400) {
      /* Unknown redirection code */
      return "Redirection";
    }

    if (response_code >= 400 && response_code < 500) {
      /* Unknown request error code */
      return "Client Error";
    }

    if (response_code >= 500 && response_code < 600) {
      /* Unknown server error code */
      return "Server Error";
    }

    /* Response code not even within reasonable range */
    return "";
  }
}

sds build_http_response(Janet res) {
  sds response_string = sdsempty();

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

            const char * code_text = response_code_text(code);
            response_string = sdscatprintf(sdsempty(), "HTTP/1.1 %d %s\n", code, code_text);

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


static Janet build_http_request(const char *method, int method_len, const char *path, int path_len, int minor_version, struct phr_header req_headers[100], size_t num_headers) {
  JanetTable *payload = janet_table(4);

  //janet_table_put(payload, janet_ckeywordv("body"), mg2janetstr(request->body));
  janet_table_put(payload, janet_ckeywordv("uri"), janet_stringv(path, path_len));
  //janet_table_put(payload, janet_ckeywordv("query-string"), janet_wrap_string(janet_cstring(method)));
  janet_table_put(payload, janet_ckeywordv("method"), janet_stringv(method, method_len));
  janet_table_put(payload, janet_ckeywordv("protocol"), janet_wrap_string(janet_cstring(sdscatprintf(sdsempty(), "HTTP 1.%d", minor_version))));
  //janet_table_put(payload, janet_ckeywordv("connection"), janet_wrap_abstract(c->user_data));

  /* Add headers */
  JanetTable *headers = janet_table(num_headers);
  for (int i = 0; i < (int)num_headers; i++) {
      if ((int)req_headers[i].value_len == 0)
          break;

      janet_table_put(headers,
              janet_stringv(req_headers[i].name, req_headers[i].name_len),
              janet_stringv(req_headers[i].value, req_headers[i].value_len));
  }

  janet_table_put(payload, janet_ckeywordv("headers"), janet_wrap_table(headers));

  return janet_wrap_table(payload);
}

int server(JanetFunction *handler, int32_t port, const uint8_t *ip_address) {
  struct sockaddr_in server_address;
  int server_fd;

  const char *method;
  size_t method_len;
  const char *path;
  size_t path_len;
  int minor_version;
  struct phr_header headers[100];
  size_t num_headers;
  size_t prevbuflen = 0;

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

  const int RECEIVE_SIZE = 1024;
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

    //printf("New events from the kernel. Count: %d\n", event_count);

    for (event_iter = 0; event_iter < event_count; event_iter++) {
      if (ev_list[event_iter].ident == server_fd) {
        //printf("New connection, setting up accept\n");
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
          //printf("Read event from client: 0x%016" PRIXPTR "\n",
           //   ev_list[event_iter].ident);

          received_bytes = recv(ev_list[event_iter].ident, receive_buf,
              sizeof(receive_buf), 0);

          if (received_bytes <= 0) {
            fprintf(stderr, "Error receiving bytes: %s\n", strerror(errno));
            close(ev_list[event_iter].ident);
            break;
          }

          receive_buf[received_bytes] = '\0';
          num_headers = sizeof(headers) / sizeof(headers[0]);
          int pret = phr_parse_request(receive_buf, received_bytes, &method, &method_len, &path, &path_len, &minor_version, headers, &num_headers, prevbuflen);

          if (pret <= 0) {
            fprintf(stderr, "Error parsing http %s\n", strerror(errno));
            close(ev_list[event_iter].ident);
            break;
          }


          Janet jarg[1];
          //jarg[0] = janet_wrap_string(janet_cstring(receive_buf));
          jarg[0] = build_http_request(method, method_len, path, path_len, minor_version, headers, num_headers);
          Janet janet_response = janet_call(handler, 1, jarg);

          sds response = build_http_response(janet_response);
          int response_size = (int)sdslen(response);

          // printf("Read %d bytes from client: 0x%016" PRIXPTR "\n",
          //     received_bytes,
          //     ev_list[event_iter].ident);
          //
          //
          // for (int i = 0; i < received_bytes; i++) {
          //   printf("%c", receive_buf[i]);
          // }
          //
          // printf("\n");

          int bytes_sent = send(ev_list[event_iter].ident,
              response, response_size, 0);

          // printf("Sent %d/%d bytes to client: 0x%016" PRIXPTR "\n", bytes_sent,
          //     response_size, ev_list[event_iter].ident);

          ev_list[event_iter].flags = ev_list[event_iter].flags ^ EV_EOF;
        }

        if (ev_list[event_iter].flags & EV_EOF) {
          //printf("EOF set for 0x%016" PRIXPTR "\n", ev_list[event_iter].ident);

          EV_SET(&ev_set, ev_list[event_iter].ident, EVFILT_READ, EV_DELETE,
              0, 0, NULL);

          if (kevent(kq, &ev_set, 1, NULL, 0, NULL) < 0) {
            fprintf(stderr, "Problem removing kevent for client: %s\n",
                strerror(errno));
            close(ev_list[event_iter].ident);
            exit(1);
          }

          close(ev_list[event_iter].ident);

          //printf("Connection closed\n");
        }
      }
    }
  }

  return 0;
}

Janet cfun_start_server(int32_t argc, Janet *argv) {
  janet_fixarity(argc, 3);

  JanetFunction *handler = janet_getfunction(argv, 0);
  int32_t port = janet_getinteger(argv, 1);
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

