#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  FILE *input;
  FILE *output;
  FILE *terminalInput;
  FILE *terminalOutput;
} Files;

Files readFile(char *argv[]) {

  Files files;

  files.input = fopen(argv[1], "r");
  files.output = fopen(argv[2], "w");
  files.terminalInput = fopen(argv[3], "r");
  files.terminalOutput = fopen(argv[4], "w");

  return files;
}

int main(int argc, char *argv[]) {

  printf("---------------------------------------------------------------------"
         "----\n");

  for (uint32_t i = 0; i < argc; i++) {
    // Outputting argument
    printf("argv[%i] = %s\n", i, argv[i]);
  }

  Files files = readFile(argv);

  return 0;
}
