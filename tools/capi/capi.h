/* Helpers shared by test_capi.c and fuzz.c: a stream read to the end and
 * reduced to a digest of every value, row by row, so two reads compare
 * whatever their batch sizes. Only the public API and nanoarrow's structs. */
#ifndef ZUCSV_CAPI_H
#define ZUCSV_CAPI_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "nanoarrow.h"
#include "zucsv.h"

typedef struct {
  int code;            /* the open's or the failing get_next()'s */
  char message[512];   /* get_last_error() at that point */
  int64_t ncol;
  int64_t nrow;        /* rows in all batches */
  int64_t nbatch;
  int64_t max_batch;   /* the longest batch */
  char formats[64];    /* the children's formats, concatenated */
  uint64_t digest;     /* FNV-1a over names and every value */
} capi_read;

static void capi_fnv(uint64_t *h, const void *p, size_t n) {
  const unsigned char *b = (const unsigned char *)p;
  for (size_t i = 0; i < n; i++) {
    *h ^= b[i];
    *h *= 1099511628211ULL;
  }
}

static int capi_bit(const void *buf, int64_t i) {
  return (((const uint8_t *)buf)[i >> 3] >> (i & 7)) & 1;
}

/* Every value of one batch, row-major, into the digest; -1 when the batch
   breaks a promise of the contract (lengths, a null row, a format). */
static int capi_digest_batch(capi_read *r, const struct ArrowSchema *schema,
                             const struct ArrowArray *a) {
  if (a->n_children != schema->n_children || a->null_count != 0 || a->offset != 0)
    return -1;
  for (int64_t j = 0; j < a->n_children; j++)
    if (a->children[j]->length != a->length)
      return -1;
  for (int64_t i = 0; i < a->length; i++) {
    for (int64_t j = 0; j < a->n_children; j++) {
      const struct ArrowArray *c = a->children[j];
      const char *f = schema->children[j]->format;
      int64_t k = c->offset + i;
      int valid = c->buffers[0] == NULL || capi_bit(c->buffers[0], k);
      capi_fnv(&r->digest, &valid, sizeof valid);
      if (!valid)
        continue;
      switch (f[0]) {
      case 'b': {
        int v = capi_bit(c->buffers[1], k);
        capi_fnv(&r->digest, &v, sizeof v);
        break;
      }
      case 'i':
        capi_fnv(&r->digest, (const int32_t *)c->buffers[1] + k, sizeof(int32_t));
        break;
      case 'g':
        capi_fnv(&r->digest, (const double *)c->buffers[1] + k, sizeof(double));
        break;
      case 'u': {
        const int32_t *off = (const int32_t *)c->buffers[1];
        int32_t len = off[k + 1] - off[k];
        if (len < 0)
          return -1;
        capi_fnv(&r->digest, &len, sizeof len);
        capi_fnv(&r->digest, (const char *)c->buffers[2] + off[k], (size_t)len);
        break;
      }
      default:
        return -1;
      }
    }
  }
  return 0;
}

/* Reads `s` to its end and releases it, exactly once. Returns r->code. */
static int capi_consume(struct ArrowArrayStream *s, int open_code, capi_read *r) {
  memset(r, 0, sizeof(*r));
  r->digest = 1469598103934665603ULL;
  r->code = open_code;
  if (!s->release)
    return r->code;
  struct ArrowSchema schema;
  schema.release = NULL;
  if (r->code == 0)
    r->code = s->get_schema(s, &schema);
  if (r->code == 0) {
    r->ncol = schema.n_children;
    for (int64_t j = 0; j < schema.n_children; j++) {
      size_t used = strlen(r->formats);
      if (used + 2 < sizeof(r->formats))
        strcat(r->formats, schema.children[j]->format);
      capi_fnv(&r->digest, schema.children[j]->name, strlen(schema.children[j]->name) + 1);
    }
    for (;;) {
      struct ArrowArray a;
      a.release = NULL;
      r->code = s->get_next(s, &a);
      if (r->code != 0) {
        if (a.release) /* the contract says released; hold it to that */
          r->code = -2;
        break;
      }
      if (!a.release)
        break;
      if (capi_digest_batch(r, &schema, &a) != 0)
        r->code = -1;
      r->nrow += a.length;
      r->nbatch++;
      if (a.length > r->max_batch)
        r->max_batch = a.length;
      a.release(&a);
      if (r->code)
        break;
    }
  }
  if (r->code > 0) {
    const char *msg = s->get_last_error(s);
    snprintf(r->message, sizeof(r->message), "%s", msg ? msg : "(null)");
  }
  if (schema.release)
    schema.release(&schema);
  s->release(s);
  return r->code;
}

#endif
