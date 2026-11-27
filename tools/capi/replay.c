/* Runs the fuzz harness over files, without libFuzzer: for a compiler that
 * lacks it (Apple clang), and for the corpus as a regression suite. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    FILE *f = fopen(argv[i], "rb");
    if (!f) {
      perror(argv[i]);
      return 2;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n > 0 ? (size_t)n : 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    LLVMFuzzerTestOneInput(buf, got);
    free(buf);
  }
  printf("replayed %d inputs\n", argc - 1);
  return 0;
}
