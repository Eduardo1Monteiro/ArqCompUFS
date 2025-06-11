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

void loadMemory(FILE *input, uint32_t offset, uint8_t *mem) {

  char *lineBuffer = (char *)(malloc(256 * sizeof(char)));
  uint32_t instruction;
  uint32_t currentAddress = offset;

  rewind(input);

  while (fgets(lineBuffer, 256, input) != NULL) {

    uint32_t memIndex = currentAddress - offset;

    sscanf(lineBuffer, "%x", &instruction);

    for (uint32_t i = 0; i < 4; i++) {
      mem[memIndex + i] = (instruction >> (8 * i)) & 0xFF;
    }

    currentAddress += 4;
  }

  for (uint32_t i = 0; i < (currentAddress - offset); i++) {
    printf("mem[%i] = %02x\n", i, mem[i]);
  }

  free(lineBuffer);

  return;
}

int main(int argc, char *argv[]) {

  printf("---------------------------------------------------------------------"
         "----\n");

  for (uint32_t i = 0; i < argc; i++) {
    printf("argv[%i] = %s\n", i, argv[i]);
  }

  Files files = readFile(argv);

  const uint32_t offset = 0x80000000;

  uint32_t pc = offset;

  uint32_t x[32] = {0};

  const char *x_label[32] = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

  uint8_t *mem = (uint8_t *)(malloc(32 * 1024));

  loadMemory(files.input, offset, mem);

  uint8_t run = 1;

  while (run) {
    const uint32_t instruction = ((uint32_t *)(mem))[(pc - offset) >> 2];
    const uint8_t opcode = instruction & 0b1111111;
    const uint8_t funct7 = instruction >> 25;
    const uint16_t imm = instruction >> 20;
    const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
    const uint8_t rs1 = (instruction & (0b11111 << 15)) >> 15;
    const uint8_t rs2 = (instruction & (0b11111 << 20)) >> 20;
    const uint8_t funct3 = (instruction & (0b111 << 12)) >> 12;
    const uint8_t rd = (instruction & (0b11111 << 7)) >> 7;
    const uint32_t imm20 = ((instruction >> 31) << 19) |
                           (((instruction & (0b11111111 << 12)) >> 12) << 11) |
                           (((instruction & (0b1 << 20)) >> 20) << 10) |
                           ((instruction & (0b1111111111 << 21)) >> 21);

    switch (opcode) {
    case 0b0010011:
      // slli
      if (funct3 == 0b001 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] << uimm;
        printf("0x%08x:slli   %s,%s,%u  %s=0x%08x<<%u=0x%08x\n", pc,
               x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
        if (rd != 0)
          x[rd] = data;
      }
      // addi
      else if (funct3 == 0b000) {
        const uint32_t simm = (imm >> 11) ? (0xFFFFF000 | imm) : imm;
        const uint32_t data = simm + x[rs1];

        printf("0x%08x:addi   %s,%s,%d  %s=0x%08x+%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      break;

    case 0b0110011:
      // add
      if (funct3 == 0b000 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] + x[rs2];

        printf("0x%08x:add   %s,%s,%s  %s=0x%08x+0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      // sub
      else if (funct3 == 0b000 && funct7 == 0b100000) {
        const uint32_t data = x[rs1] - x[rs2];

        printf("0x%08x:sub   %s,%s,%s  %s=0x%08x-0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      // xor
      else if (funct3 == 0b100 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] ^ x[rs2];

        printf("0x%08x:xor    %s,%s,%s   %s=0x%08x^0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      // or
      else if (funct3 == 0b110 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] | x[rs2];

        printf("0x%08x:or    %s,%s,%s   %s=0x%08x|0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      // and
      else if (funct3 == 0b111 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] & x[rs2];

        printf("0x%08x:and    %s,%s,%s   %s=0x%08x&0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);

        if (rd != 0) {
          x[rd] = data;
        }
      }
      break;

    case 0b1110011:
      // ebreak
      if (funct3 == 0b000 && imm == 1) {
        printf("0x%08x:ebreak\n", pc);
        const uint32_t previous = ((uint32_t *)(mem))[(pc - 4 - offset) >> 2];
        const uint32_t next = ((uint32_t *)(mem))[(pc + 4 - offset) >> 2];
        if (previous == 0x01f01013 && next == 0x40705013)
          run = 0;
      }
      break;
    case 0b1101111:
      // jal
      const uint32_t simm = (imm20 >> 19) ? (0xFFF00000 | imm20) : (imm20);
      const uint32_t address = pc + (simm << 1);
      printf("0x%08x:jal    %s,0x%05x    pc=0x%08x,%s=0x%08x\n", pc,
             x_label[rd], imm, address, x_label[rd], pc + 4);
      if (rd != 0)
        x[rd] = pc + 4;
      pc = address - 4;
      break;
    default:
      printf("error: unknown instruction opcode at pc = 0x%08x\n", pc);
      run = 0;
    }
    pc = pc + 4;
  }

  return 0;
}
