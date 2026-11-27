# The R-free C suite for zucsv's core and stream (#23): no R headers, no R.
#
#   make -C tools/capi ZUARROW=<zuarrow checkout>         build and run
#   make -C tools/capi ZUARROW=... tsan CC=clang          under ThreadSanitizer
#   make -C tools/capi ZUARROW=... tsan-negative CC=clang its control: must report
#   make -C tools/capi ZUARROW=... asan CC=clang          under ASan + UBSan
#   make -C tools/capi ZUARROW=... replay                 the fuzz harness over corpus/
#
# It compiles the sources libzucsv.a holds (src/core.c, convert.c, stream.c
# and zsv) and nanoarrow from a zuarrow source checkout at the commit
# DESCRIPTION's Remotes names: the same vendor/nanoarrow/nanoarrow.c
# zuarrow builds into libzuarrow.a, with the defines it uses. If it stops
# building with an R header error, R has leaked into the core.
#
# GNU make is fine here: tools/capi is not in the package (.Rbuildignore).

CC      ?= cc
ZUARROW ?= ../../../zuarrow
ROOT    := ../..
SRC     := $(ROOT)/src
BUILD   := build
SAN     :=
CFLAGS  ?= -O1 -g

NANOARROW := $(ZUARROW)/src/vendor/nanoarrow
INC     := -I$(SRC) -I$(ROOT)/inst/include -I$(SRC)/vendor/zsv/include -I$(NANOARROW)
DEFS    := -DZSV_EXPORT= -DNANOARROW_DLL= -DNANOARROW_NAMESPACE=zuarrow -DZUCSV_NANOARROW_SOURCE
WARN    := -Wall -Wextra -Wno-unused-parameter
OWN     := -std=c99 -D_POSIX_C_SOURCE=200809L $(CFLAGS) $(SAN) $(WARN) $(INC) $(DEFS)
VEND    := $(CFLAGS) $(SAN) -w $(INC) $(DEFS)
LIBS    := -lpthread -lm

CORE    := $(SRC)/core.c $(SRC)/convert.c $(SRC)/stream.c
VSRC    := $(SRC)/vendor/zsv/src/zsv.c $(NANOARROW)/nanoarrow.c

.PHONY: all run tsan tsan-negative asan replay clean
all: run

$(BUILD)/libcore.a: $(CORE) $(VSRC) $(SRC)/core.h $(SRC)/strtod_body.h $(ROOT)/inst/include/zucsv.h
	@mkdir -p $(BUILD)
	$(CC) $(OWN) -c $(SRC)/core.c -o $(BUILD)/core.o
	$(CC) $(OWN) -c $(SRC)/convert.c -o $(BUILD)/convert.o
	$(CC) $(OWN) -c $(SRC)/stream.c -o $(BUILD)/stream.o
	$(CC) $(VEND) -c $(SRC)/vendor/zsv/src/zsv.c -o $(BUILD)/zsv.o
	$(CC) $(VEND) -c $(NANOARROW)/nanoarrow.c -o $(BUILD)/nanoarrow.o
	rm -f $@ && ar rcs $@ $(BUILD)/core.o $(BUILD)/convert.o $(BUILD)/stream.o $(BUILD)/zsv.o $(BUILD)/nanoarrow.o

$(BUILD)/test_capi: test_capi.c capi.h $(BUILD)/libcore.a
	$(CC) $(OWN) test_capi.c $(BUILD)/libcore.a $(LIBS) -o $@

$(BUILD)/replay: fuzz.c replay.c capi.h $(BUILD)/libcore.a
	$(CC) $(OWN) fuzz.c replay.c $(BUILD)/libcore.a $(LIBS) -o $@

run: $(BUILD)/test_capi
	$(BUILD)/test_capi

# Each in a directory of its own, so no object is shared between flavours.
# halt_on_error and exitcode: a report fails the run, whatever the tests say.
tsan:
	TSAN_OPTIONS='halt_on_error=1 exitcode=66' $(MAKE) BUILD=build-tsan SAN='-fsanitize=thread' CFLAGS='-O1 -g' run

# Passes only when TSan reports: two threads sharing one stream.
tsan-negative:
	$(MAKE) BUILD=build-tsan SAN='-fsanitize=thread' CFLAGS='-O1 -g' build-tsan/libcore.a
	$(CC) -std=c99 -D_POSIX_C_SOURCE=200809L -O1 -g -fsanitize=thread $(WARN) $(INC) $(DEFS) \
	  -DCAPI_TSAN_NEGATIVE test_capi.c build-tsan/libcore.a $(LIBS) -o build-tsan/negative
	TSAN_OPTIONS='halt_on_error=1 exitcode=66' build-tsan/negative > build-tsan/negative.log 2>&1; \
	  if grep -q 'ThreadSanitizer: data race' build-tsan/negative.log; then \
	    echo "TSan reported the shared stream, as it must"; \
	  else cat build-tsan/negative.log; echo "FAIL: TSan did not report two threads on one stream"; exit 1; fi

asan:
	$(MAKE) BUILD=build-asan SAN='-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer' CFLAGS='-O1 -g' run

replay: $(BUILD)/replay
	$(BUILD)/replay corpus/*

clean:
	rm -rf build build-tsan build-asan
