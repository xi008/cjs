#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <curl/curl.h>

#include "../sites/tv.cctv.com/native/cctv_h5e_decryptor.c"

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  (void)prio;
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "[%s] ", tag);
  vfprintf(stderr, fmt, ap);
  fputc('\n', stderr);
  va_end(ap);
  return 0;
}

static const char b64u_tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static int b64u_val(int c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

static unsigned char *b64u_decode(const char *in, size_t *out_len) {
  size_t n = strlen(in), cap = n / 4 * 3 + 4, i, o = 0;
  unsigned char *out = malloc(cap ? cap : 1);
  unsigned acc = 0, bits = 0;
  if (!out) return NULL;
  for (i = 0; i < n; i++) {
    int v = b64u_val((unsigned char)in[i]);
    if (v < 0) continue;
    acc = (acc << 6) | (unsigned)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out[o++] = (unsigned char)((acc >> bits) & 0xff);
    }
  }
  *out_len = o;
  return out;
}

static char *b64u_encode(const unsigned char *in, size_t len) {
  char *out = malloc(len * 2 + 8);
  size_t i, o = 0;
  unsigned acc = 0, bits = 0;
  if (!out) return NULL;
  for (i = 0; i < len; i++) {
    acc = (acc << 8) | in[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      out[o++] = b64u_tbl[(acc >> bits) & 63];
    }
  }
  if (bits > 0) out[o++] = b64u_tbl[(acc << (6 - bits)) & 63];
  out[o] = 0;
  return out;
}

static char *b64u_encode_str(const char *in) {
  return b64u_encode((const unsigned char *)in, strlen(in));
}

typedef struct {
  char *data;
  size_t len;
  int code;
  char ctype[64];
} fetch_result;

static size_t curl_write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
  fetch_result *r = (fetch_result *)userdata;
  size_t total = size * nmemb;
  char *p = realloc(r->data, r->len + total + 1);
  if (!p) return 0;
  r->data = p;
  memcpy(r->data + r->len, ptr, total);
  r->len += total;
  r->data[r->len] = 0;
  return total;
}

static size_t curl_header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
  fetch_result *r = (fetch_result *)userdata;
  if (nitems > 14 && strncasecmp(buffer, "Content-Type:", 13) == 0) {
    size_t i = 13, j = 0;
    while (i < nitems && (buffer[i] == ' ' || buffer[i] == '\t')) i++;
    while (i < nitems && buffer[i] != '\r' && buffer[i] != '\n' && j < sizeof(r->ctype) - 1)
      r->ctype[j++] = buffer[i++];
    r->ctype[j] = 0;
  }
  (void)size;
  return nitems;
}

static int is_h5e_url(const char *url) {
  return strstr(url, ".m3u8") != NULL || strstr(url, ".ts") != NULL ||
         strstr(url, ".m4s") != NULL || strstr(url, "hls") != NULL;
}

static int fetch_url(const char *url, fetch_result *r, const char *referer, long timeout_sec) {
  CURL *curl = curl_easy_init();
  if (!curl) return -1;
  memset(r, 0, sizeof(*r));
  strcpy(r->ctype, "application/octet-stream");
  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, r);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, r);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_sec);
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "Mozilla/5.0 (Linux; Android 12) AppleWebKit/537.36");
  if (referer) curl_easy_setopt(curl, CURLOPT_REFERER, referer);
  CURLcode res = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  curl_easy_cleanup(curl);
  r->code = (int)code;
  return res == CURLE_OK ? 0 : -1;
}

static char *resolve_url(const char *base, const char *rel) {
  if (!strncmp(rel, "http://", 7) || !strncmp(rel, "https://", 8)) return strdup(rel);
  char *out;
  if (rel[0] == '/') {
    const char *p = strstr(base, "://");
    if (p) {
      p += 3;
      size_t host_len = 0;
      while (p[host_len] && p[host_len] != '/') host_len++;
      size_t prefix = (size_t)(p - base) + host_len;
      out = malloc(prefix + strlen(rel) + 1);
      if (!out) return NULL;
      memcpy(out, base, prefix);
      memcpy(out + prefix, rel, strlen(rel) + 1);
      return out;
    }
    return strdup(rel);
  }
  const char *slash = strrchr(base, '/');
  if (!slash) return strdup(rel);
  size_t prefix = (size_t)(slash - base + 1);
  out = malloc(prefix + strlen(rel) + 1);
  if (!out) return NULL;
  memcpy(out, base, prefix);
  strcpy(out + prefix, rel);
  return out;
}

static char *rewrite_playlist(char *text, const char *playlist_url) {
  size_t cap = strlen(text) * 3 + 4096;
  char *out = malloc(cap);
  size_t o = 0;
  char *save = NULL;
  char *line = strtok_r(text, "\n", &save);
  int wrote_ext = 0;
  if (!out) return NULL;
  while (line) {
    size_t ll = strlen(line);
    int skip = 0;
    if (ll && line[ll - 1] == '\r') line[ll - 1] = 0, ll--;
    if (line[0] == '#') {
      if (!strncmp(line, "#EXT-X-MAP:", 11) || !strncmp(line, "#EXT-X-KEY:", 11) ||
          !strncmp(line, "#EXT-X-MEDIA:", 13) || !strncmp(line, "#EXT-X-I-FRAME-STREAM-INF:", 26)) {
        char *uri = strstr(line, "URI=\"");
        if (uri) {
          char *start = uri + 5;
          char *end = strchr(start, '"');
          if (end) {
            char saved = *end;
            *end = 0;
            char *abs = resolve_url(playlist_url, start);
            *end = saved;
            if (abs) {
              char *newuri = malloc(strlen(abs) + 32);
              if (newuri) {
                sprintf(newuri, "URI=\"/url=%s\"", abs);
                size_t pre = (size_t)(uri + 5 - line);
                char *rest = end + 1;
                size_t need = o + pre + strlen(newuri) + strlen(rest) + 4;
                if (need < cap) {
                  memcpy(out + o, line, pre);
                  o += pre;
                  strcpy(out + o, newuri);
                  o += strlen(newuri);
                  strcpy(out + o, rest);
                  o += strlen(rest);
                  out[o++] = '\n';
                  wrote_ext = 1;
                }
                free(newuri);
              }
              free(abs);
            }
          }
        }
      }
      if (!wrote_ext) {
        size_t need = o + strlen(line) + 2;
        if (need < cap) {
          strcpy(out + o, line);
          o += strlen(line);
          out[o++] = '\n';
        }
      }
      wrote_ext = 0;
    } else if (ll > 0) {
      char *abs = resolve_url(playlist_url, line);
      if (abs) {
        size_t need = o + strlen(abs) + 16;
        if (need < cap) {
          sprintf(out + o, "/url=%s\n", abs);
          o += strlen(out + o);
        }
        free(abs);
      }
    }
    (void)skip;
    line = strtok_r(NULL, "\n", &save);
  }
  out[o] = 0;
  return out;
}

static int decrypt_in_place(unsigned char *buf, size_t len) {
  int trapped = 0, decrypted = 0;
  g_decrypt_generation = __sync_add_and_fetch(&g_cancel_generation, 0);
  g_decrypt_cancelled = 0;
  wasm_rt_trap_t trap = (wasm_rt_trap_t)wasm_rt_try(g_wasm_rt_jmp_buf);
  if (trap == WASM_RT_TRAP_NONE) {
    decrypted = decrypt_transport_stream((uint8_t *)buf, (const uint8_t *)buf, (size_t)len);
  } else {
    fprintf(stderr, "[cctv_h5e] wasm trap: %s\n", wasm_rt_strerror(trap));
    trapped = 1;
  }
  if (trapped) return -1;
  if (g_decrypt_cancelled) return -2;
  (void)decrypted;
  return 0;
}

static void send_response(int fd, int code, const char *ctype, const void *body, size_t blen) {
  char hdr[512];
  const char *msg = code == 200 ? "OK" : code == 206 ? "Partial Content" :
                    code == 404 ? "Not Found" : code == 405 ? "Method Not Allowed" :
                    code == 403 ? "Forbidden" : code == 502 ? "Bad Gateway" : "Error";
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Access-Control-Allow-Origin: *\r\nAccept-Ranges: bytes\r\n"
                   "Connection: close\r\n\r\n",
                   code, msg, ctype, blen);
  if (send(fd, hdr, n, 0) < 0) return;
  if (!body || !blen) return;
  size_t off = 0;
  while (off < blen) {
    ssize_t w = send(fd, (const char *)body + off, blen - off, 0);
    if (w <= 0) break;
    off += (size_t)w;
  }
}

static void send_headers_only(int fd, int code, const char *ctype, size_t blen) {
  char hdr[512];
  const char *msg = code == 200 ? "OK" : code == 404 ? "Not Found" : "Error";
  int n = snprintf(hdr, sizeof(hdr),
                   "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                   "Access-Control-Allow-Origin: *\r\nAccept-Ranges: bytes\r\n"
                   "Connection: close\r\n\r\n",
                   code, msg, ctype, blen);
  send(fd, hdr, n, 0);
}

static int hex_val(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static void url_decode(char *s) {
  char *r = s, *w = s;
  while (*r) {
    if (r[0] == '%' && hex_val((unsigned char)r[1]) >= 0 && hex_val((unsigned char)r[2]) >= 0) {
      *w++ = (char)((hex_val((unsigned char)r[1]) << 4) | hex_val((unsigned char)r[2]));
      r += 3;
    } else if (r[0] == '+' && r == s) {
      r++;
    } else {
      *w++ = *r++;
    }
  }
  *w = 0;
}

static void handle_client(int fd) {
  char req[8192];
  ssize_t n = recv(fd, req, sizeof(req) - 1, 0);
  if (n <= 0) { close(fd); return; }
  req[n] = 0;
  char method[8] = {0}, path[8192] = {0};
  if (sscanf(req, "%7s %8191s", method, path) != 2 ||
      (strcmp(method, "GET") && strcmp(method, "HEAD"))) {
    send_response(fd, 405, "text/plain", "method not allowed", 19);
    close(fd);
    return;
  }
  int is_head = (strcmp(method, "HEAD") == 0);

  if (!strcmp(path, "/healthz")) {
    send_response(fd, 200, "text/plain", "ok", 2);
    close(fd);
    return;
  }

  char *url = NULL;
  if (!strncmp(path, "/p/", 3)) {
    size_t ulen = 0;
    unsigned char *urlbytes = b64u_decode(path + 3, &ulen);
    if (!urlbytes) { send_response(fd, 400, "text/plain", "bad b64", 6); close(fd); return; }
    url = malloc(ulen + 1);
    memcpy(url, urlbytes, ulen);
    url[ulen] = 0;
    free(urlbytes);
  } else if (!strncmp(path, "/url=", 5)) {
    url = strdup(path + 5);
    if (!url) { close(fd); return; }
    url_decode(url);
  } else {
    send_response(fd, 404, "text/plain",
                  "usage: /url=<m3u8-or-ts-url>  or  /p/<base64url(url)>", 55);
    close(fd);
    return;
  }

  char *q = strchr(url, '?');
  int is_playlist = 0;
  if (q) *q = 0;
  is_playlist = strstr(url, ".m3u8") != NULL;
  if (q) *q = '?';

  fetch_result fr;
  int fok = fetch_url(url, &fr, "https://tv.cctv.com/", 20);
  if (fok != 0 || !fr.data) {
    fprintf(stderr, "[h5e] fetch FAILED %s\n", url);
    send_response(fd, 502, "text/plain", "upstream fetch failed", 21);
    free(url);
    if (fr.data) free(fr.data);
    close(fd);
    return;
  }
  if (fr.code >= 400) {
    fprintf(stderr, "[h5e] upstream %d %s\n", fr.code, url);
    send_response(fd, fr.code, "text/plain", fr.data, fr.len);
    free(url);
    free(fr.data);
    close(fd);
    return;
  }

  if (is_playlist) {
    char *rewritten = rewrite_playlist(fr.data, url);
    if (rewritten) {
      size_t rl = strlen(rewritten);
      fprintf(stderr, "[h5e] playlist %zu -> %zu bytes %s\n", fr.len, rl, url);
      if (is_head) send_headers_only(fd, 200, "application/vnd.apple.mpegurl", rl);
      else send_response(fd, 200, "application/vnd.apple.mpegurl", rewritten, rl);
      free(rewritten);
    } else {
      send_response(fd, 502, "text/plain", "rewrite failed", 14);
    }
  } else if (is_h5e_url(url)) {
    int rc = decrypt_in_place((unsigned char *)fr.data, fr.len);
    if (rc == -1) {
      fprintf(stderr, "[h5e] decrypt trap %s\n", url);
      send_response(fd, 502, "text/plain", "decrypt trap", 12);
    } else {
      const char *ct = fr.ctype[0] ? fr.ctype : "video/mp2t";
      fprintf(stderr, "[h5e] ts %zu bytes rc=%d %s\n", fr.len, rc, url);
      if (is_head) send_headers_only(fd, 200, ct, fr.len);
      else send_response(fd, 200, ct, fr.data, fr.len);
    }
  } else {
    const char *ct = fr.ctype[0] ? fr.ctype : "application/octet-stream";
    fprintf(stderr, "[h5e] passthrough %zu bytes %s\n", fr.len, url);
    if (is_head) send_headers_only(fd, 200, ct, fr.len);
    else send_response(fd, 200, ct, fr.data, fr.len);
  }
  free(url);
  free(fr.data);
  close(fd);
}

int main(int argc, char **argv) {
  int port = 18081;
  int i;
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
  }
  signal(SIGPIPE, SIG_IGN);
  int srv = socket(AF_INET, SOCK_STREAM, 0);
  if (srv < 0) { perror("socket"); return 1; }
  int on = 1;
  setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons((uint16_t)port);
  if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
  if (listen(srv, 16) < 0) { perror("listen"); return 1; }
  socklen_t alen = sizeof(addr);
  getsockname(srv, (struct sockaddr *)&addr, &alen);
  printf("h5e_cli listening on http://127.0.0.1:%d\n", ntohs(addr.sin_port));
  fflush(stdout);
  for (;;) {
    int c = accept(srv, NULL, NULL);
    if (c < 0) continue;
    handle_client(c);
  }
  return 0;
}