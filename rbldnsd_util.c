/* $Id$
 * Common utility routines for rbldnsd.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include "rbldnsd.h"

#define skipspace(s) while(*s == ' ' || *s == '\t') ++s

#define digit(c) ((c) >= '0' && (c) <= '9')
#define d2n(c) ((c) - '0') 

char *parse_uint32(char *s, unsigned char nb[4]) {
  unsigned char *t = (unsigned char*)s;
  unsigned n = 0;
  if (!digit(*t))
    return NULL;
  do { 
    if (n > 0xffffffffu / 10) return 0;
    if (n * 10 > 0xffffffffu - d2n(*t)) return 0;
    n = n * 10 + d2n(*t++);
  } while(digit(*t));
  nb[0] = n>>24; nb[1] = n>>16; nb[2] = n>>8; nb[3] = n;
  skipspace(t);
  return (char*)t;
}

char *parse_ttl(char *s, unsigned char ttl[4], const unsigned char defttl[4]) {
  s = parse_uint32(s, ttl);
  if (s)
    if (memcmp(ttl, "\0\0\0\0", 4) == 0)
      memcpy(ttl, defttl, 4);
  return s;
}

char *parse_dn(char *s, unsigned char *dn, unsigned *dnlenp) {
  char *n = s;
  unsigned l;
  while(*n && *n != ' ' && *n != '\t') ++n;
  if (*n) *n++ = '\0';
  if (!*s) return NULL;
  if ((l = dns_ptodn(s, dn, DNS_MAXDN)) == 0)
    return NULL;
  dns_dntol(dn, dn);
  if (dnlenp) *dnlenp = l;
  skipspace(n);
  return n;
}

int
readdslines(FILE *f, struct zonedataset *zds,
            int (*dslpfn)(struct zonedataset *zds, char *line, int lineno)) {
#define bufsiz 512
  char _buf[bufsiz+4], *line, *eol;
#define buf (_buf+4)  /* keep room for 4 IP octets in addrtxt() */
  int lineno = 0, noeol = 0;
  while(fgets(buf, bufsiz, f)) {
    eol = buf + strlen(buf) - 1;
    if (eol < buf) /* can this happen? */
      continue;
    if (noeol) { /* read parts of long line up to \n */
      if (*eol == '\n')
        noeol = 0;
      continue;
    }
    ++lineno;
    if (*eol == '\n')
      --eol;
    else {
      if (!feof(f))
        dswarn(lineno, "long line (truncated)");
      noeol = 1; /* mark it to be read above */
    }
    /* skip whitespace */
    line = buf;
    while(*line == ' ' || *line == '\t')
      ++line;
    while(eol >= line && (*eol == ' ' || *eol == '\t'))
      --eol;
    eol[1] = '\0';
    if (line[0] == '$' ||
        ((line[0] == '#' || line[0] == ':') && line[1] == '$')) {
      int r = zds_special(zds, line[0] == '$' ? line + 1 : line + 2);
      if (!r)
        dswarn(lineno, "invalid or unrecognized special entry");
      else if (r < 0)
        return 0;
      continue;
    }
    if (line[0] && line[0] != '#')
      if (!dslpfn(zds, line, lineno))
        return 0;
  }
  return 1;
#undef buf
}

/* helper routine for dntoip4addr() */

static const unsigned char *dnotoa(const unsigned char *q, unsigned *ap) {
  if (*q < 1 || *q > 3) return NULL;
  if (q[1] < '0' || q[1] > '9') return NULL;
  *ap = q[1] - '0';
  if (*q == 1) return q + 2;
  if (q[2] < '0' || q[2] > '9') return NULL;
  *ap = *ap * 10 + (q[2] - '0');
  if (*q == 2) return q + 3;
  if (q[3] < '0' || q[3] > '9') return NULL;
  *ap = *ap * 10 + (q[3] - '0');
  return *ap > 255 ? NULL : q + 4;
}

/* parse DN (as in 4.3.2.1.in-addr.arpa) to ip4addr_t */

unsigned dntoip4addr(const unsigned char *q, ip4addr_t *ap) {
  ip4addr_t a = 0, o;
  if ((q = dnotoa(q, &o)) == NULL) return 0; a |= o;
  if (!*q) { *ap = a << 24; return 1; }
  if ((q = dnotoa(q, &o)) == NULL) return 0; a |= o << 8;
  if (!*q) { *ap = a << 16; return 2; }
  if ((q = dnotoa(q, &o)) == NULL) return 0; a |= o << 16;
  if (!*q) { *ap = a << 8; return 3; }
  if ((q = dnotoa(q, &o)) == NULL) return 0; a |= o << 24;
  if (!*q) { *ap = a; return 4; }
  return 0;
}

int parse_a_txt(char *str, const char **rrp, const char def_a[4]) {
  char *rr;
  while(*str == ' ' || *str == '\t') ++str;
  if (*str == ':') {
    ip4addr_t a;
    unsigned bits = ip4addr(str + 1, &a, &str);
    if (!a || !bits) return 0;
    if (bits == 8)
      a |= IP4A_LOOPBACK; /* only last digit in 127.0.0.x */
    skipspace(str);
    if (*str == ':')
      ++str;
    else if (*str != '\0' && *str != '#' && *str != '\0')
      return 0;
    skipspace(str);
    rr = (unsigned char*)str - 4;
    rr[0] = a >> 24; rr[1] = a >> 16; rr[2] = a >> 8; rr[3] = a;
  }
  else {
    rr = (unsigned char*)str - 4;
    memcpy(rr, def_a, 4);
  }
  if (*str != '#' && *str != '\0') {
    int len = strlen(str);
    str += len > 255 ? 255 : len;
  }
  *str = '\0';
  *rrp = rr;
  return 1 + (str - rr);
}

char *emalloc(unsigned size) {
  void *ptr = malloc(size);
  if (!ptr)
    oom();
  return ptr;
}

char *ezalloc(unsigned size) {
  void *ptr = calloc(1, size);
  if (!ptr)
    oom();
  return ptr;
}

char *erealloc(void *ptr, unsigned size) {
  void *nptr = realloc(ptr, size);
  if (!nptr)
    oom();
  return nptr;
}

char *ememdup(const void *buf, unsigned len) {
  char *b = emalloc(len);
  if (b)
    memcpy(b, buf, len);
  return b;
}

char *estrdup(const char *str) {
  return ememdup(str, strlen(str) + 1);
}

/* what a mess... this routine is to work around various snprintf
 * implementations.  It never return <1 or value greather than
 * size of buffer: i.e. it returns number of chars _actually written_
 * to a buffer.
 * Maybe replace this with an alternative (simplistic) implementation,
 * only %d/%u/%s, with additional %S to print untrusted data replacing
 * control chars with something sane, and to print `...' for arguments
 * that aren't fit (e.g. "%.5s", "1234567" will print `12...') ?
 */

int vssprintf(char *buf, int bufsz, const char *fmt, va_list ap) {
  int r = vsnprintf(buf, bufsz, fmt, ap);
  return r < 0 ? 0 : r >= bufsz ? bufsz - 1 : r;
}

int ssprintf(char *buf, int bufsz, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  bufsz = vssprintf(buf, bufsz, fmt, ap);
  va_end(ap);
  return bufsz;
}
