/* zucsv_open_stream(): read_csv() as an Arrow C stream (#23, design SS30).
 *
 * Opening runs pass 1 (core.c), so every error read_csv() raises is
 * returned before a schema exists. Each get_next() then runs pass 2 for up
 * to batch_rows rows, appending each cell to its column with the nanoarrow
 * builders zuarrow bundles (zuarrow/nanoarrow.h, linked from libzuarrow.a):
 * logical to `b`, integer to `i`, double to `g`, character to `u`, a cell
 * equal to an na string to a null. No R anywhere: this file, core.c and
 * convert.c are libzucsv.a. */

#include "core.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "zuarrow.h"
#include "zuarrow/nanoarrow.h"

#if !defined(ZUARROW_ABI_VERSION) || ZUARROW_ABI_VERSION != 1
#error "zucsv's stream is written against zuarrow's ABI 1"
#endif

typedef struct {
  zucsv_core core;
  struct ArrowSchema schema; /* the stream's schema, copied on request */
  int64_t batch_rows;
  /* Owned copies of the options' strings, which need not outlive open. */
  char *path;
  char **na;
  size_t n_na_alloc;
  zucsv_type *forced;
  int done;
} zucsv_stream;

static zucsv_stream *zucsv_stream_of(struct ArrowArrayStream *s) {
  return (zucsv_stream *)s->private_data;
}

/* The message of a failed stream, or of a nanoarrow call that failed. */
static int zucsv_stream_fail_code(zucsv_stream *st, int code, const char *what) {
  return zucsv_core_fail(&st->core, code ? code : EINVAL, "%s", what);
}

static int zucsv_get_schema(struct ArrowArrayStream *s, struct ArrowSchema *out) {
  zucsv_stream *st = zucsv_stream_of(s);
  if (st->core.code)
    return st->core.code;
  int code = ArrowSchemaDeepCopy(&st->schema, out);
  if (code != NANOARROW_OK)
    return zucsv_stream_fail_code(st, code, "Ran out of memory copying the CSV schema");
  return 0;
}

static const char *zucsv_get_last_error(struct ArrowArrayStream *s) {
  zucsv_stream *st = zucsv_stream_of(s);
  return st->core.code ? st->core.message : NULL;
}

/* Appends the parser's current row, cell by cell. Pass 1 proved every
   cell valid for its column, so only an allocation can fail here. */
static int zucsv_append_row(zucsv_stream *st, struct ArrowArray *out) {
  zucsv_core *c = &st->core;
  for (ptrdiff_t j = 0; j < c->ncol; j++) {
    struct ArrowArray *child = out->children[j];
    struct zsv_cell cell = zsv_get_cell(c->parser, (size_t)j);
    int code;
    if (zucsv_is_na(&c->na, cell.str, cell.len)) {
      code = ArrowArrayAppendNull(child, 1);
    } else {
      switch (c->types[j]) {
      case ZUCSV_LOGICAL:
        code = ArrowArrayAppendInt(child, zucsv_as_logical(cell.str, cell.len));
        break;
      case ZUCSV_INTEGER: {
        int value = 0;
        zucsv_is_integer(cell.str, cell.len, &value);
        code = ArrowArrayAppendInt(child, value);
        break;
      }
      case ZUCSV_DOUBLE: {
        double value = zucsv_core_as_double(c, cell.str, cell.len);
        if (c->code)
          return c->code;
        code = ArrowArrayAppendDouble(child, value);
        break;
      }
      default: {
        struct ArrowStringView view;
        view.data = (const char *)cell.str;
        view.size_bytes = (int64_t)cell.len;
        code = ArrowArrayAppendString(child, view);
        break;
      }
      }
    }
    if (code != NANOARROW_OK)
      return zucsv_stream_fail_code(st, code == ENOMEM ? ENOMEM : code,
                                    "Ran out of memory building a CSV batch");
  }
  int code = ArrowArrayFinishElement(out);
  if (code != NANOARROW_OK)
    return zucsv_stream_fail_code(st, code, "Ran out of memory building a CSV batch");
  return 0;
}

static int zucsv_get_next(struct ArrowArrayStream *s, struct ArrowArray *out) {
  zucsv_stream *st = zucsv_stream_of(s);
  zucsv_core *c = &st->core;
  out->release = NULL;
  if (c->code)
    return c->code;
  if (st->done)
    return 0;

  struct ArrowError error;
  int code = ArrowArrayInitFromSchema(out, &st->schema, &error);
  if (code == NANOARROW_OK)
    code = ArrowArrayStartAppending(out);
  if (code != NANOARROW_OK) {
    if (out->release)
      out->release(out);
    return zucsv_stream_fail_code(st, code, "Ran out of memory starting a CSV batch");
  }

  int64_t n = 0;
  while (n < st->batch_rows) {
    int r = zucsv_core_next_row(c);
    if (r < 0)
      goto fail;
    if (r == 0) {
      st->done = 1;
      break;
    }
    if (zucsv_append_row(st, out))
      goto fail;
    n++;
  }

  /* Rows ended exactly at the last batch: no empty batch, just the end. */
  if (n == 0) {
    out->release(out);
    out->release = NULL;
    return 0;
  }

  code = ArrowArrayFinishBuildingDefault(out, &error);
  if (code != NANOARROW_OK) {
    zucsv_stream_fail_code(st, code, error.message);
    goto fail;
  }
  return 0;

fail:
  if (out->release)
    out->release(out);
  out->release = NULL;
  return c->code ? c->code : EINVAL;
}

static void zucsv_stream_free(zucsv_stream *st) {
  zucsv_core_close(&st->core);
  if (st->schema.release)
    st->schema.release(&st->schema);
  free(st->path);
  if (st->na) {
    for (size_t i = 0; i < st->n_na_alloc; i++)
      free(st->na[i]);
    free(st->na);
  }
  free(st->core.na.str);
  free(st->core.na.len);
  free(st->forced);
  free(st);
}

static void zucsv_release(struct ArrowArrayStream *s) {
  if (!s->release)
    return;
  zucsv_stream_free(zucsv_stream_of(s));
  s->release = NULL;
  s->private_data = NULL;
}

static char *zucsv_strdup(const char *s) {
  size_t n = strlen(s);
  char *copy = (char *)malloc(n + 1);
  if (copy)
    memcpy(copy, s, n + 1);
  return copy;
}

/* Options into the core: the struct as the caller compiled it, its strings
   copied so they need not outlive this call. */
static int zucsv_stream_configure(zucsv_stream *st, const zucsv_options *options) {
  zucsv_core *c = &st->core;
  zucsv_options opts;
  zucsv_options_init(&opts);
  if (options) {
    if (options->struct_size < offsetof(zucsv_options, batch_rows) + sizeof(int64_t))
      return zucsv_core_fail(c, EINVAL, "zucsv_options.struct_size is %u; zucsv_options_init() sets it",
                             (unsigned)options->struct_size);
    memcpy(&opts, options, sizeof(opts));
  }

  c->delim = opts.delimiter;
  c->want_header = opts.header;
  c->strtod_mode = opts.strtod_mode;
  c->strtod = opts.strtod;

  if (opts.batch_rows < 1)
    return zucsv_core_fail(c, EINVAL, "batch_rows must be at least 1");
  st->batch_rows = opts.batch_rows;

  if (opts.n_na < 0 || (opts.n_na > 0 && !opts.na))
    return zucsv_core_fail(c, EINVAL, "na must hold n_na strings");
  if (opts.n_na > 0) {
    size_t n = (size_t)opts.n_na;
    st->na = (char **)calloc(n, sizeof(char *));
    c->na.str = (const char **)calloc(n, sizeof(char *));
    c->na.len = (size_t *)calloc(n, sizeof(size_t));
    if (!st->na || !c->na.str || !c->na.len)
      return zucsv_core_fail(c, ENOMEM, "Ran out of memory copying the na strings");
    st->n_na_alloc = n;
    for (size_t i = 0; i < n; i++) {
      if (!opts.na[i])
        return zucsv_core_fail(c, EINVAL, "'na' may not contain NA");
      st->na[i] = zucsv_strdup(opts.na[i]);
      if (!st->na[i])
        return zucsv_core_fail(c, ENOMEM, "Ran out of memory copying the na strings");
      c->na.str[i] = st->na[i];
      c->na.len[i] = strlen(st->na[i]);
    }
    c->na.n = (ptrdiff_t)n;
  }

  if (opts.col_types) {
    if (opts.n_col_types < 1)
      return zucsv_core_fail(c, EINVAL, "col_types must hold n_col_types names, at least one");
    size_t n = (size_t)opts.n_col_types;
    st->forced = (zucsv_type *)calloc(n, sizeof(zucsv_type));
    if (!st->forced)
      return zucsv_core_fail(c, ENOMEM, "Ran out of memory copying col_types");
    for (size_t i = 0; i < n; i++) {
      const char *name = opts.col_types[i];
      if (name && strcmp(name, "logical") == 0)
        st->forced[i] = ZUCSV_LOGICAL;
      else if (name && strcmp(name, "integer") == 0)
        st->forced[i] = ZUCSV_INTEGER;
      else if (name && strcmp(name, "double") == 0)
        st->forced[i] = ZUCSV_DOUBLE;
      else if (name && strcmp(name, "character") == 0)
        st->forced[i] = ZUCSV_CHARACTER;
      else
        return zucsv_core_fail(c, EINVAL,
                               "`col_types` must contain only logical, integer, double, character. "
                               "Unknown: %s.",
                               name ? name : "NULL");
    }
    c->forced = st->forced;
    c->n_forced = (ptrdiff_t)n;
  } else if (opts.n_col_types != 0) {
    return zucsv_core_fail(c, EINVAL, "col_types is NULL but n_col_types is not 0");
  }
  return zucsv_core_check_options(c);
}

static int zucsv_build_schema(zucsv_stream *st) {
  zucsv_core *c = &st->core;
  struct ArrowSchema *schema = &st->schema;
  int code = ArrowSchemaInitFromType(schema, NANOARROW_TYPE_STRUCT);
  if (code == NANOARROW_OK)
    code = ArrowSchemaSetName(schema, "");
  /* The rows themselves are never null: the struct is not nullable. */
  if (code == NANOARROW_OK)
    schema->flags = 0;
  if (code == NANOARROW_OK)
    code = ArrowSchemaAllocateChildren(schema, (int64_t)c->ncol);
  for (ptrdiff_t j = 0; code == NANOARROW_OK && j < c->ncol; j++) {
    enum ArrowType type;
    switch (c->types[j]) {
    case ZUCSV_LOGICAL:
      type = NANOARROW_TYPE_BOOL;
      break;
    case ZUCSV_INTEGER:
      type = NANOARROW_TYPE_INT32;
      break;
    case ZUCSV_DOUBLE:
      type = NANOARROW_TYPE_DOUBLE;
      break;
    default:
      type = NANOARROW_TYPE_STRING;
      break;
    }
    code = ArrowSchemaInitFromType(schema->children[j], type);
    if (code == NANOARROW_OK)
      code = ArrowSchemaSetName(schema->children[j], c->names[j]);
  }
  if (code != NANOARROW_OK)
    return zucsv_stream_fail_code(st, code == ENOMEM ? ENOMEM : code,
                                  "Ran out of memory building the CSV schema");
  return 0;
}

/* Open: configure, pass 1, the schema, and the rewind for pass 2. */
static int zucsv_stream_open(zucsv_stream *st, const zucsv_options *options) {
  zucsv_core *c = &st->core;
  if (zucsv_stream_configure(st, options))
    return c->code;
  if (zucsv_core_pass1(c))
    return c->code;
  if (zucsv_build_schema(st))
    return c->code;
  if (c->ncol == 0 || c->nrow == 0) {
    st->done = 1;
    return 0;
  }
  return zucsv_core_rewind(c);
}

/* The two entries differ only in the source. */
static int zucsv_stream_make(const char *path, const char *data, size_t len,
                             const zucsv_options *options, struct ArrowArrayStream *out) {
  out->release = NULL;
  out->private_data = NULL;
  zucsv_stream *st = (zucsv_stream *)calloc(1, sizeof(zucsv_stream));
  if (!st)
    return ENOMEM;
  zucsv_core_init(&st->core);
  st->schema.release = NULL;

  out->get_schema = zucsv_get_schema;
  out->get_next = zucsv_get_next;
  out->get_last_error = zucsv_get_last_error;
  out->release = zucsv_release;
  out->private_data = st;

  if (data) {
    st->core.data = data;
    st->core.len = len;
  } else if (!path) {
    return zucsv_core_fail(&st->core, EINVAL, "path is NULL");
  } else {
    st->path = zucsv_strdup(path);
    if (!st->path)
      return zucsv_core_fail(&st->core, ENOMEM, "Ran out of memory copying the path");
    st->core.path = st->path;
  }
  int code = zucsv_stream_open(st, options);
  if (code)
    zucsv_core_close(&st->core); /* the error state holds no file */
  return code;
}

int zucsv_open_stream(const char *path, const zucsv_options *options,
                      struct ArrowArrayStream *out) {
  if (!out)
    return EINVAL;
  return zucsv_stream_make(path, NULL, 0, options, out);
}

int zucsv_open_stream_text(const char *data, size_t len, const zucsv_options *options,
                           struct ArrowArrayStream *out) {
  if (!out)
    return EINVAL;
  if (!data)
    data = "";
  return zucsv_stream_make(NULL, data, len, options, out);
}

static const char *const zucsv_default_na[] = {"", "NA"};

void zucsv_options_init(zucsv_options *options) {
  memset(options, 0, sizeof(*options));
  options->struct_size = (uint32_t)sizeof(zucsv_options);
  options->header = ZUCSV_HEADER_YES;
  options->delimiter = ',';
  options->na = zucsv_default_na;
  options->n_na = 2;
  options->col_types = NULL;
  options->n_col_types = 0;
  options->strtod_mode = ZUCSV_STRTOD_UNSET;
  options->strtod = NULL;
  options->batch_rows = ZUCSV_BATCH_ROWS;
}

uint32_t zucsv_abi_version(void) {
  return ZUCSV_ABI_VERSION;
}
