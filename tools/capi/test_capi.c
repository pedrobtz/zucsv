/* zucsv's C API without R (#23): values, refusals, error streams, the
 * lifecycle, the progress hook, a file changed between the passes, and the
 * entry point on eight threads that are not R's (run under TSan by
 * `make tsan`). Doubles are compared with values exact in binary, since
 * there is no R here to probe; the R suite holds them to as.numeric(). */

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "capi.h"
#include "core.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    checks++;                                                                                      \
    if (!(cond)) {                                                                                 \
      failures++;                                                                                  \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);                    \
    }                                                                                              \
  } while (0)

static char *write_temp(const char *bytes, size_t n) {
  char *path = strdup("/tmp/zucsv-capi-XXXXXX");
  int fd = mkstemp(path);
  if (fd < 0) {
    perror("mkstemp");
    exit(2);
  }
  if (write(fd, bytes, n) != (ssize_t)n) {
    perror("write");
    exit(2);
  }
  close(fd);
  return path;
}

static void rewrite(const char *path, const char *bytes) {
  FILE *f = fopen(path, "wb");
  fwrite(bytes, 1, strlen(bytes), f);
  fclose(f);
}

static zucsv_options opts_default(void) {
  zucsv_options o;
  zucsv_options_init(&o);
  /* No R to probe: the verbatim double instance is R's loop in double. */
  o.strtod_mode = ZUCSV_STRTOD_DOUBLE_VERBATIM;
  return o;
}

static const char *TYPES =
    "l,i,d,s\n"
    "TRUE,1,1.5,a\n"
    "NA,,-Inf,\"x,y\"\n"
    "false,-2147483647,0.25,\xc3\xa9\n";

static void test_values(void) {
  char *path = write_temp(TYPES, strlen(TYPES));
  zucsv_options o = opts_default();
  struct ArrowArrayStream s;
  CHECK(zucsv_open_stream(path, &o, &s) == 0);

  struct ArrowSchema schema;
  CHECK(s.get_schema(&s, &schema) == 0);
  CHECK(strcmp(schema.format, "+s") == 0);
  CHECK(schema.n_children == 4);
  CHECK(schema.flags == 0);
  const char *formats[] = {"b", "i", "g", "u"};
  const char *names[] = {"l", "i", "d", "s"};
  for (int j = 0; j < 4; j++) {
    CHECK(strcmp(schema.children[j]->format, formats[j]) == 0);
    CHECK(strcmp(schema.children[j]->name, names[j]) == 0);
    CHECK(schema.children[j]->flags == ARROW_FLAG_NULLABLE);
  }

  struct ArrowArray a;
  CHECK(s.get_next(&s, &a) == 0);
  CHECK(a.release != NULL && a.length == 3 && a.null_count == 0);
  struct ArrowArray **c = a.children;
  /* l: TRUE, null, false */
  CHECK(c[0]->null_count == 1 && capi_bit(c[0]->buffers[0], 0) && !capi_bit(c[0]->buffers[0], 1));
  CHECK(capi_bit(c[0]->buffers[1], 0) == 1 && capi_bit(c[0]->buffers[1], 2) == 0);
  /* i: 1, null, -2147483647 */
  const int32_t *iv = (const int32_t *)c[1]->buffers[1];
  CHECK(c[1]->null_count == 1 && iv[0] == 1 && iv[2] == -2147483647);
  /* d: 1.5, -Inf, 0.25 */
  const double *dv = (const double *)c[2]->buffers[1];
  CHECK(c[2]->null_count == 0 && dv[0] == 1.5 && dv[1] < -1e308 && dv[2] == 0.25);
  /* s: "a", "x,y", "e acute" */
  const int32_t *off = (const int32_t *)c[3]->buffers[1];
  const char *bytes = (const char *)c[3]->buffers[2];
  CHECK(off[1] - off[0] == 1 && memcmp(bytes + off[0], "a", 1) == 0);
  CHECK(off[2] - off[1] == 3 && memcmp(bytes + off[1], "x,y", 3) == 0);
  CHECK(off[3] - off[2] == 2 && memcmp(bytes + off[2], "\xc3\xa9", 2) == 0);
  a.release(&a);

  /* the end, and the end again */
  CHECK(s.get_next(&s, &a) == 0 && a.release == NULL);
  CHECK(s.get_next(&s, &a) == 0 && a.release == NULL);
  CHECK(s.get_last_error(&s) == NULL);
  schema.release(&schema);
  s.release(&s);
  CHECK(s.release == NULL);
  remove(path);
  free(path);
}

/* ZUCSV_STRTOD_CALL with libc's strtod, which is correctly rounded: on
   these values it and R's loop agree. */
static void test_call_mode(void) {
  const char *csv = "x\n1.5\n0.1\nInf\nNaN\n" ;
  zucsv_options o = opts_default();
  o.strtod_mode = ZUCSV_STRTOD_CALL;
  o.strtod = strtod;
  struct ArrowArrayStream s;
  CHECK(zucsv_open_stream_text(csv, strlen(csv), &o, &s) == 0);
  capi_read call;
  capi_consume(&s, 0, &call);
  o = opts_default();
  CHECK(zucsv_open_stream_text(csv, strlen(csv), &o, &s) == 0);
  capi_read verbatim;
  capi_consume(&s, 0, &verbatim);
  CHECK(call.code == 0 && call.nrow == 4 && call.digest == verbatim.digest);
}

static int open_code(const char *csv, zucsv_options *o, char *msg, size_t n) {
  struct ArrowArrayStream s;
  int code = zucsv_open_stream_text(csv, strlen(csv), o, &s);
  if (s.release) {
    const char *m = s.get_last_error(&s);
    snprintf(msg, n, "%s", m ? m : "");
    s.release(&s);
  }
  return code;
}

static void test_refusals(void) {
  char msg[512];
  zucsv_options o;

  zucsv_options_init(&o); /* strtod_mode left unset */
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);
  CHECK(strstr(msg, "strtod_mode must be set") != NULL);

  o = opts_default();
  o.strtod_mode = ZUCSV_STRTOD_CALL; /* without a function */
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);
  CHECK(strstr(msg, "needs a strtod function") != NULL);

  o = opts_default();
  o.struct_size = 8;
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);
  CHECK(strstr(msg, "struct_size") != NULL);

  o = opts_default();
  o.header = 7;
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);

  o = opts_default();
  o.delimiter = '"';
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);

  o = opts_default();
  o.n_na = 1;
  o.na = NULL;
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);

  o = opts_default();
  const char *na_with_null[] = {"NA", NULL};
  o.na = na_with_null;
  o.n_na = 2;
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);
  CHECK(strstr(msg, "'na' may not contain NA") != NULL);

  o = opts_default();
  o.n_col_types = 1; /* with col_types NULL */
  CHECK(open_code("a\n1\n", &o, msg, sizeof msg) == EINVAL);

  /* NULL options are the defaults, whose strtod_mode is unset */
  CHECK(open_code("a\n1\n", NULL, msg, sizeof msg) == EINVAL);

  o = opts_default();
  CHECK(zucsv_open_stream("x", &o, NULL) == EINVAL);
  struct ArrowArrayStream s;
  CHECK(zucsv_open_stream(NULL, &o, &s) == EINVAL);
  s.release(&s);

  CHECK(zucsv_abi_version() == ZUCSV_ABI_VERSION);
  CHECK(zucsv_strtod_usable(ZUCSV_STRTOD_DOUBLE_VERBATIM) && !zucsv_strtod_usable(9));
  CHECK(zucsv_strtod_probe_value(0) != NULL && zucsv_strtod_probe_value(-1) == NULL &&
        zucsv_strtod_probe_value(1000) == NULL);
}

static void test_error_stream(void) {
  zucsv_options o = opts_default();
  struct ArrowArrayStream s;
  const char *csv = "a,b\n1,2,3\n";
  CHECK(zucsv_open_stream_text(csv, strlen(csv), &o, &s) == EINVAL);
  CHECK(s.release != NULL);
  struct ArrowSchema schema;
  struct ArrowArray a;
  CHECK(s.get_schema(&s, &schema) == EINVAL);
  CHECK(s.get_next(&s, &a) == EINVAL && a.release == NULL);
  CHECK(strcmp(s.get_last_error(&s), "CSV row 2 has 3 fields; expected 2") == 0);
  s.release(&s);

  char *missing = strdup("/tmp/zucsv-capi-missing-XXXXXX");
  int fd = mkstemp(missing);
  close(fd);
  remove(missing);
  CHECK(zucsv_open_stream(missing, &o, &s) == EIO);
  char want[256];
  snprintf(want, sizeof want, "Cannot open CSV file: %s", missing);
  CHECK(strcmp(s.get_last_error(&s), want) == 0);
  s.release(&s);
  free(missing);
}

/* A file rewritten after open: shorter, longer, wider. get_next() fails
   with EIO and read_csv()'s message, and keeps failing. */
static void test_drift(void) {
  const char *after[] = {"x\n1\n", "x\n1\n2\n3\n4\n", "x,y\n1,2\n3,4\n5,6\n"};
  for (int k = 0; k < 3; k++) {
    const char *before = "x\n1\n2\n3\n";
    char *path = write_temp(before, strlen(before));
    zucsv_options o = opts_default();
    struct ArrowArrayStream s;
    CHECK(zucsv_open_stream(path, &o, &s) == 0);
    rewrite(path, after[k]);
    capi_read r;
    char want[512];
    snprintf(want, sizeof want, "CSV file changed while it was being read: %s", path);
    /* consume releases; read the message through capi_read */
    capi_consume(&s, 0, &r);
    CHECK(r.code == EIO);
    CHECK(strcmp(r.message, want) == 0);
    remove(path);
    free(path);
  }
}

static int progress_calls;
static void count_progress(void *ctx) {
  (void)ctx;
  progress_calls++;
}

/* The hook read_csv() checks interrupts in: every 16384 records, header and
   blank records included, in both passes. */
static void test_progress(void) {
  const int nrec = 40000;
  size_t cap = (size_t)nrec * 8 + 16;
  char *csv = malloc(cap);
  size_t len = 0;
  len += (size_t)sprintf(csv + len, "x\n");
  for (int i = 1; i < nrec; i++)
    len += (size_t)sprintf(csv + len, "%d\n", i);
  zucsv_core c;
  zucsv_core_init(&c);
  c.data = csv;
  c.len = len;
  c.strtod_mode = ZUCSV_STRTOD_DOUBLE_VERBATIM;
  c.progress = count_progress;
  progress_calls = 0;
  CHECK(zucsv_core_pass1(&c) == 0);
  CHECK(c.nrow == nrec - 1);
  CHECK(progress_calls == nrec / ZUCSV_INTERRUPT_INTERVAL);
  progress_calls = 0;
  CHECK(zucsv_core_rewind(&c) == 0);
  int rows = 0, r;
  while ((r = zucsv_core_next_row(&c)) == 1) {
    CHECK(c.at == rows);
    rows++;
  }
  CHECK(r == 0 && rows == nrec - 1);
  CHECK(progress_calls == nrec / ZUCSV_INTERRUPT_INTERVAL);
  CHECK(zucsv_core_next_row(&c) == 0); /* after the end */
  zucsv_core_close(&c);
  zucsv_core_close(&c); /* twice is safe */
  free(csv);
}

/* A mixed table, big enough for several batches. */
static char *big_table(size_t *len) {
  const int n = 20000;
  size_t cap = (size_t)n * 64;
  char *csv = malloc(cap);
  size_t k = (size_t)sprintf(csv, "l,i,d,s\n");
  for (int i = 0; i < n; i++)
    k += (size_t)snprintf(csv + k, cap - k, "%s,%d,%d.%03de%d,%s\n",
                          i % 7 == 0 ? "NA" : (i % 2 ? "TRUE" : "false"), i % 11 ? i : -i,
                          i % 1000, i % 997, (i % 40) - 20, i % 5 == 0 ? "" : (i % 3 ? "x" : "\"q,\"\"r\""));
  *len = k;
  return csv;
}

static void test_batches_agree(void) {
  size_t len;
  char *csv = big_table(&len);
  uint64_t first = 0;
  int64_t sizes[] = {ZUCSV_BATCH_ROWS, 1, 7, 4096, 20000, 20001};
  for (size_t k = 0; k < sizeof sizes / sizeof *sizes; k++) {
    zucsv_options o = opts_default();
    o.batch_rows = sizes[k];
    struct ArrowArrayStream s;
    capi_read r;
    capi_consume(&s, zucsv_open_stream_text(csv, len, &o, &s), &r);
    CHECK(r.code == 0 && r.nrow == 20000 && strcmp(r.formats, "bigu") == 0);
    CHECK(r.max_batch == (sizes[k] < 20000 ? sizes[k] : 20000));
    if (k == 0)
      first = r.digest;
    CHECK(r.digest == first);
  }
  free(csv);
}

typedef struct {
  const char *path;
  int64_t batch_rows;
  uint64_t want;
  int bad;
} thread_job;

static void *thread_read(void *arg) {
  thread_job *job = (thread_job *)arg;
  for (int rep = 0; rep < 4; rep++) {
    zucsv_options o = opts_default();
    o.batch_rows = job->batch_rows;
    struct ArrowArrayStream s;
    capi_read r;
    capi_consume(&s, zucsv_open_stream(job->path, &o, &s), &r);
    if (r.code != 0 || r.digest != job->want || r.nrow != 20000)
      job->bad++;
  }
  return NULL;
}

/* The entry point on threads that are not the main one, eight at once over
   the same file, each read equal to the main thread's. */
static void test_threads(void) {
  size_t len;
  char *csv = big_table(&len);
  char *path = write_temp(csv, len);
  free(csv);
  zucsv_options o = opts_default();
  struct ArrowArrayStream s;
  capi_read ref;
  capi_consume(&s, zucsv_open_stream(path, &o, &s), &ref);
  CHECK(ref.code == 0);

  pthread_t t[8];
  thread_job jobs[8];
  for (int k = 0; k < 8; k++) {
    jobs[k].path = path;
    jobs[k].batch_rows = 1000 + 777 * k;
    jobs[k].want = ref.digest;
    jobs[k].bad = 0;
    CHECK(pthread_create(&t[k], NULL, thread_read, &jobs[k]) == 0);
  }
  for (int k = 0; k < 8; k++) {
    pthread_join(t[k], NULL);
    CHECK(jobs[k].bad == 0);
  }
  remove(path);
  free(path);
}

#ifdef CAPI_TSAN_NEGATIVE
/* The negative control for `make tsan`: two threads read one stream at
   once, which the contract forbids (one stream, one thread at a time). It
   passes only if ThreadSanitizer reports the race, so a clean `make tsan`
   is known to be a run TSan was watching. */
static void *shared_read(void *arg) {
  struct ArrowArrayStream *s = (struct ArrowArrayStream *)arg;
  for (int k = 0; k < 50; k++) {
    struct ArrowArray a;
    if (s->get_next(s, &a) != 0 || !a.release)
      break;
    a.release(&a);
  }
  return NULL;
}

int main(void) {
  size_t len;
  char *csv = big_table(&len);
  zucsv_options o = opts_default();
  o.batch_rows = 100;
  struct ArrowArrayStream s;
  if (zucsv_open_stream_text(csv, len, &o, &s) != 0)
    return 2;
  pthread_t t[2];
  pthread_create(&t[0], NULL, shared_read, &s);
  pthread_create(&t[1], NULL, shared_read, &s);
  pthread_join(t[0], NULL);
  pthread_join(t[1], NULL);
  s.release(&s);
  free(csv);
  printf("no race reported\n");
  return 0;
}
#else
int main(void) {
  test_values();
  test_call_mode();
  test_refusals();
  test_error_stream();
  test_drift();
  test_progress();
  test_batches_agree();
  test_threads();
  if (failures) {
    fprintf(stderr, "%d of %d checks failed\n", failures, checks);
    return 1;
  }
  printf("zucsv C API: %d checks passed\n", checks);
  return 0;
}
#endif
