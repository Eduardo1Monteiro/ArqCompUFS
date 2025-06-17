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

  if (files.input == NULL) {
    perror("FATAL: Error opening input file");
    exit(EXIT_FAILURE);
  }

  files.output = fopen(argv[2], "w");
  files.terminalInput = fopen(argv[3], "r");
  files.terminalOutput = fopen(argv[4], "w");

  return files;
}

void loadMemory(FILE *input, uint32_t offset, uint8_t *mem) {

  char lineBuffer[256];
  uint32_t currentAddress = 0;

  rewind(input);

  while (fgets(lineBuffer, sizeof(lineBuffer), input) != NULL) {

    if (lineBuffer[0] == '@') {
      sscanf(lineBuffer, "@%x", &currentAddress);
    } else {
      char *ptr = lineBuffer;
      int chars_scanned = 0;
      unsigned int byte_val = 0;

      while (sscanf(ptr, "%x%n", &byte_val, &chars_scanned) == 1) {

        if (currentAddress >= offset) {
          uint32_t mem_index = currentAddress - offset;

          if (mem_index < (32 * 1024)) {
            mem[mem_index] = (uint8_t)byte_val;
          }
        }

        currentAddress++;
        ptr += chars_scanned;
      }
    }
  }

  return;
}

int32_t signedImmediate(uint16_t imm) {
  return (imm & 0x800) ? (0xFFFFF000 | imm) : imm;
}

void loadRd(uint32_t data, uint8_t rd, uint32_t x[]) {
  if (rd != 0) {
    x[rd] = data;
  }
}

int main(int argc, char *argv[]) {

  printf("---------------------------------------------------------------------"
         "----\n");

  Files files = readFile(argv);

  const uint32_t offset = 0x80000000;

  uint32_t pc = offset;

  uint32_t x[32] = {0};

  const char *x_label[32] = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

  uint8_t *mem = (uint8_t *)(malloc(32 * 1024));

  if (mem == NULL) {
    perror("FATAL: Failed to allocate memory");
    exit(EXIT_FAILURE);
  }

  loadMemory(files.input, offset, mem);

  uint8_t run = 1;

  while (run) {
    // Decoder
    uint32_t instruction = mem[pc - offset] | (mem[pc - offset + 1] << 8) |
                           (mem[pc - offset + 2] << 16) |
                           (mem[pc - offset + 3] << 24);
    const uint8_t opcode = instruction & 0x7F;
    const uint8_t funct7 = (instruction >> 25) & 0x7F;
    const uint16_t imm = instruction >> 20;
    const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
    const uint8_t rs1 = (instruction >> 15) & 0x1F;
    const uint8_t rs2 = (instruction >> 20) & 0x1F;
    const uint8_t funct3 = (instruction >> 12) & 0x07;
    const uint8_t rd = (instruction >> 7) & 0x1F;
    const uint8_t immS7 = (instruction >> 25) & 0b1111111;
    const uint8_t immS5 = (instruction >> 7) & 0b11111;
    const uint16_t immS = (immS7 << 5) | immS5;
    const uint32_t imm20 = ((instruction >> 31) << 19) |
                           (((instruction & (0b11111111 << 12)) >> 12) << 11) |
                           (((instruction & (0b1 << 20)) >> 20) << 10) |
                           ((instruction & (0b1111111111 << 21)) >> 21);
    const uint32_t imm12 = (instruction >> 19) & 0x1000; // bit 12 is inst[31]
    const uint32_t imm11 = (instruction & 0x80) << 4;    // bit 11 is inst[7]
    const uint32_t imm10_5 =
        (instruction >> 20) & 0x7E0; // bits 10:5 are inst[30:25]
    const uint32_t imm4_1 =
        (instruction >> 7) & 0x1E; // bits 4:1 are inst[11:8]

    const uint32_t imm_b_unsigned = imm12 | imm11 | imm10_5 | imm4_1;

    const int32_t branchImm = (imm_b_unsigned & 0x1000)
                                  ? (0xFFFFE000 | imm_b_unsigned)
                                  : imm_b_unsigned;

    switch (opcode) {
    case 0b0010011: // I-Type
      // slli
      if (funct3 == 0b001 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] << uimm;
        printf("0x%08x:slli   %s,%s,%u  %s=0x%08x<<%u=0x%08x\n", pc,
               x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm, data);
        fprintf(files.output, "0x%08x:slli   %s,%s,%u  %s=0x%08x<<%u=0x%08x\n",
                pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], imm,
                data);
        loadRd(data, rd, x);
      }
      // addi
      else if (funct3 == 0b000) {
        const int32_t simm = signedImmediate(imm);
        const int32_t data = simm + ((int32_t)x[rs1]);

        printf("0x%08x:addi   %s,%s,%d  %s=0x%08x+%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);
        fprintf(files.output, "0x%08x:addi   %s,%s,%d  %s=0x%08x+%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // andi
      else if (funct3 == 0b111) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] & simm;

        printf("0x%08x:andi   %s,%s,%d  %s=0x%08x&%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);
        fprintf(files.output, "0x%08x:andi   %s,%s,%d  %s=0x%08x&%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // ori
      else if (funct3 == 0b110) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] | simm;

        printf("0x%08x:ori   %s,%s,%d  %s=0x%08x|%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);
        fprintf(files.output, "0x%08x:ori   %s,%s,%d  %s=0x%08x|%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // xori
      // later add in here the not instruction
      else if (funct3 == 0b100) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] ^ simm;

        printf("0x%08x:xori   %s,%s,%d  %s=0x%08x^%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);
        fprintf(files.output, "0x%08x:xori   %s,%s,%d  %s=0x%08x^%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // slti
      else if (funct3 == 0b010) {
        int32_t simm = signedImmediate(imm);
        uint32_t data = ((int32_t)x[rs1] < simm) ? 1 : 0;

        printf("0x%08x:slti   %s,%s,%d  %s=0x%08x<%d=0x%08x\n", pc, x_label[rd],
               x_label[rs1], simm, x_label[rd], x[rs1], simm, data);
        fprintf(files.output, "0x%08x:slti   %s,%s,%d  %s=0x%08x<%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // sltiu
      else if (funct3 == 0b011) {
        uint32_t simm = signedImmediate(imm);
        uint32_t data = ((uint32_t)x[rs1] < (uint32_t)simm) ? 1 : 0;

        printf("0x%08x:sltiu   %s,%s,%d  %s=0x%08x<%d=0x%08x\n", pc,
               x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
               data);
        fprintf(files.output, "0x%08x:sltiu   %s,%s,%d  %s=0x%08x<%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      }
      // srli
      else if (funct3 == 0b101 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] >> uimm;

        printf("0x%08x:srli   %s,%s,%d  %s=0x%08x>>%d=0x%08x\n", pc,
               x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
               data);
        fprintf(files.output, "0x%08x:srli   %s,%s,%d  %s=0x%08x>>%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
                data);
        loadRd(data, rd, x);
      }
      // srai
      else if (funct3 == 0b101 && funct7 == 0b0100000) {
        const uint32_t data = ((int32_t)x[rs1]) >> uimm;

        printf("0x%08x:srai   %s,%s,%d  %s=0x%08x>>%d=0x%08x\n", pc,
               x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
               data);
        fprintf(files.output, "0x%08x:srai   %s,%s,%d  %s=0x%08x>>%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
                data);
        loadRd(data, rd, x);
      }
      break;
    case 0b0110111: // LUI
      uint32_t immU = instruction & 0xFFFFF000;
      uint32_t data = immU;

      printf("0x%08x:lui    %s,0x%08x   %s=0x%08x\n", pc, x_label[rd],
             (immU >> 12), x_label[rd], data);
      fprintf(files.output, "0x%08x:lui    %s,0x%08x   %s=0x%08x\n", pc,
              x_label[rd], (immU >> 12), x_label[rd], data);
      loadRd(data, rd, x);
      break;
    case 0b0010111: // AUIPC
      immU = instruction & 0xFFFFF000;
      data = immU + pc;

      printf("0x%08x:auipc    %s,0x%08x   %s=0x%08x\n", pc, x_label[rd],
             (immU >> 12), x_label[rd], data);
      fprintf(files.output, "0x%08x:auipc    %s,0x%08x   %s=0x%08x\n", pc,
              x_label[rd], (immU >> 12), x_label[rd], data);
      loadRd(data, rd, x);
      break;
    case 0b0000011: // L-Type
      // lb
      if (funct3 == 0b000) {
        const int32_t simm = signedImmediate(imm);
        const uint32_t address = x[rs1] + simm;
        const uint32_t index = address - offset;
        const int8_t byte = mem[index];
        const int32_t data = (int8_t)byte;

        printf("0x%08x:lb    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
               x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        fprintf(files.output,
                "0x%08x:lb    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        loadRd(data, rd, x);
      }
      // lh
      else if (funct3 == 0b001) {
        const int32_t simm = signedImmediate(imm);
        const uint32_t address = x[rs1] + simm;
        const uint32_t index = address - offset;
        const int16_t halfWord = mem[index] | (mem[index + 1] << 8);
        const int32_t data = (int16_t)halfWord;

        printf("0x%08x:lh    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
               x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        fprintf(files.output,
                "0x%08x:lh    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        loadRd(data, rd, x);
      }
      // lbu
      else if (funct3 == 0b100) {
        const int32_t simm = signedImmediate(imm);
        const uint32_t address = x[rs1] + simm;
        const uint32_t index = address - offset;
        const uint8_t byte = mem[index];
        const uint32_t data = (uint8_t)byte;

        printf("0x%08x:lbu    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
               x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        fprintf(files.output,
                "0x%08x:lbu    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        loadRd(data, rd, x);
      }
      // lhu
      else if (funct3 == 0b101) {
        const int32_t simm = signedImmediate(imm);
        const uint32_t address = x[rs1] + simm;
        const uint32_t index = address - offset;
        const uint16_t halfWord = mem[index] | (mem[index + 1] << 8);
        const uint32_t data = (uint16_t)halfWord;

        printf("0x%08x:lhu    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
               x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        fprintf(files.output,
                "0x%08x:lhu    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        loadRd(data, rd, x);
      }
      // lw
      else if (funct3 == 0b010) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t address = x[rs1] + simm;
        const uint32_t index = address - offset;
        const uint32_t data = mem[index] | (mem[index + 1] << 8) |
                              (mem[index + 2] << 16) | (mem[index + 3] << 24);

        printf("0x%08x:lw    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
               x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        fprintf(files.output,
                "0x%08x:lw    %s,0x%08x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        loadRd(data, rd, x);
      }
      break;
    case 0b1100111:
      // jalr
      if (funct3 == 0b000) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = pc + 4;
        uint32_t address = x[rs1] + simm;
        address = address & ~1;

        printf("0x%08x:jalr     %s,%s,0x%08x    pc=0x%08x+0x%08x,%s=0x%08x\n",
               pc, x_label[rd], x_label[rs1], simm, x[rs1], simm, x_label[rd],
               data);
        fprintf(files.output,
                "0x%08x:jalr     %s,%s,0x%08x    pc=0x%08x+0x%08x,%s=0x%08x\n",
                pc, x_label[rd], x_label[rs1], simm, x[rs1], simm, x_label[rd],
                data);

        loadRd(data, rd, x);

        pc = address - 4;
      }
      break;
    case 0b0100011: // S-Type
      // sw
      if (funct3 == 0b010) {
        const int32_t simm = signedImmediate(immS);
        const uint32_t address = x[rs1] + simm;
        const int32_t data = x[rs2];
        const uint32_t index = address - offset;

        mem[index + 0] = (data >> 0) & 0xFF;
        mem[index + 1] = (data >> 8) & 0xFF;
        mem[index + 2] = (data >> 16) & 0xFF;
        mem[index + 3] = (data >> 24) & 0xFF;

        printf("0x%08x:sw    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
               x_label[rs2], imm, x_label[rs1], address, data);
        fprintf(files.output,
                "0x%08x:sw    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], imm, x_label[rs1], address, data);
      }
      // sh
      else if (funct3 == 0b001) {
        const int32_t simm = signedImmediate(immS);
        const uint32_t address = x[rs1] + simm;
        const int32_t data = x[rs2];
        const uint32_t index = address - offset;

        mem[index + 0] = (data >> 0) & 0xFF;
        mem[index + 1] = (data >> 8) & 0xFF;

        printf("0x%08x:sh    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
               x_label[rs2], imm, x_label[rs1], address, data);
        fprintf(files.output,
                "0x%08x:sh    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], imm, x_label[rs1], address, data);
      }
      // sb
      else if (funct3 == 0b000) {
        const int32_t simm = signedImmediate(immS);
        const uint32_t address = x[rs1] + simm;
        const int32_t data = x[rs2];
        const uint32_t index = address - offset;

        mem[index + 0] = (data >> 0) & 0xFF;

        printf("0x%08x:sb    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
               x_label[rs2], imm, x_label[rs1], address, data);
        fprintf(files.output,
                "0x%08x:sb    %s,0x%08x(%s)   mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], imm, x_label[rs1], address, data);
      }
      break;

    case 0b0110011: // R-Type
      // add
      if (funct3 == 0b000 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] + x[rs2];

        printf("0x%08x:add   %s,%s,%s  %s=0x%08x+0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:add   %s,%s,%s  %s=0x%08x+0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // sub
      else if (funct3 == 0b000 && funct7 == 0b0100000) {
        const uint32_t data = x[rs1] - x[rs2];

        printf("0x%08x:sub   %s,%s,%s  %s=0x%08x-0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:sub   %s,%s,%s  %s=0x%08x-0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // xor
      else if (funct3 == 0b100 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] ^ x[rs2];

        printf("0x%08x:xor    %s,%s,%s   %s=0x%08x^0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:xor    %s,%s,%s   %s=0x%08x^0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // or
      else if (funct3 == 0b110 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] | x[rs2];

        printf("0x%08x:or    %s,%s,%s   %s=0x%08x|0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:or    %s,%s,%s   %s=0x%08x|0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // and
      else if (funct3 == 0b111 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] & x[rs2];

        printf("0x%08x:and    %s,%s,%s   %s=0x%08x&0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:and    %s,%s,%s   %s=0x%08x&0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // slt
      else if (funct3 == 0b010 && funct7 == 0b0000000) {
        const uint32_t data = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0;

        printf("0x%08x:slt    %s,%s,%s   %s=0x%08x<0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:slt    %s,%s,%s   %s=0x%08x<0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // sltu
      else if (funct3 == 0b011 && funct7 == 0b0000000) {
        const uint32_t data = ((uint32_t)x[rs1] < (uint32_t)x[rs2]) ? 1 : 0;

        printf("0x%08x:sltu    %s,%s,%s   %s=0x%08x<0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:sltu    %s,%s,%s   %s=0x%08x<0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // sll
      else if (funct3 == 0b001 && funct7 == 0b0000000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const uint32_t data = x[rs1] << shift;

        printf("0x%08x:sll    %s,%s,%s   %s=0x%08x<<0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:sll    %s,%s,%s   %s=0x%08x<<0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // srl
      else if (funct3 == 0b101 && funct7 == 0b0000000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const uint32_t data = x[rs1] >> shift;

        printf("0x%08x:srl    %s,%s,%s   %s=0x%08x>>0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:srl    %s,%s,%s   %s=0x%08x>>0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // sra
      else if (funct3 == 0b101 && funct7 == 0b0100000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const int32_t data = ((int32_t)x[rs1]) >> shift;

        printf("0x%08x:sra    %s,%s,%s   %s=0x%08x>>>0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:sra    %s,%s,%s   %s=0x%08x>>>0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // mul
      else if (funct3 == 0b000 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const int64_t v2 = (int32_t)x[rs2];
        const int64_t product = v1 * v2;
        const uint32_t data = product;

        printf("0x%08x:mul   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:mul   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // mulh
      else if (funct3 == 0b001 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const int64_t v2 = (int32_t)x[rs2];
        const int64_t product = (int64_t)v1 * (int64_t)v2;
        const uint32_t data = product >> 32;

        printf("0x%08x:mulh   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:mulh   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // mulhsu
      else if (funct3 == 0b010 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const uint64_t v2 = (uint32_t)x[rs2];
        const int64_t product = (int64_t)v1 * (uint64_t)v2;
        const uint32_t data = product >> 32;

        printf("0x%08x:mulhsu   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:mulhsu   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // mulhu
      else if (funct3 == 0b011 && funct7 == 0b0000001) {
        const uint64_t v1 = (int32_t)x[rs1];
        const uint64_t v2 = (int32_t)x[rs2];
        const uint64_t product = (uint64_t)v1 * (uint64_t)v2;
        const uint32_t data = product >> 32;

        printf("0x%08x:mulhu   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:mulhu   %s,%s,%s  %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // div
      else if (funct3 == 0b100 && funct7 == 0b0000001) {
        int32_t data;

        if (x[rs2] == 0) {
          data = 0xFFFFFFFF;
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0x80000000;
        } else {
          data = (int32_t)x[rs1] / (int32_t)x[rs2];
        }

        printf("0x%08x:div   %s,%s,%s  %s=0x%08x/0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:div   %s,%s,%s  %s=0x%08x/0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // divu
      else if (funct3 == 0b101 && funct7 == 0b0000001) {
        uint32_t data;

        if (x[rs2] == 0) {
          data = 0xFFFFFFFF;
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0x80000000;
        } else {
          data = (uint32_t)x[rs1] / (uint32_t)x[rs2];
        }

        printf("0x%08x:divu   %s,%s,%s  %s=0x%08x/0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:divu   %s,%s,%s  %s=0x%08x/0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // rem
      else if (funct3 == 0b110 && funct7 == 0b0000001) {
        int32_t data;

        if (x[rs2] == 0) {
          data = x[rs1];
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0;
        } else {
          data = (int32_t)x[rs1] % (int32_t)x[rs2];
        }

        printf("0x%08x:rem   %s,%s,%s  %s=0x%08x%%0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:rem   %s,%s,%s  %s=0x%08x%%0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      // remu
      else if (funct3 == 0b111 && funct7 == 0b0000001) {
        uint32_t data;

        if (x[rs2] == 0) {
          data = x[rs1];
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0;
        } else {
          data = (uint32_t)x[rs1] % (uint32_t)x[rs2];
        }

        printf("0x%08x:remu   %s,%s,%s  %s=0x%08x%%0x%08x=0x%08x\n", pc,
               x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
               x[rs2], data);
        fprintf(files.output,
                "0x%08x:remu   %s,%s,%s  %s=0x%08x%%0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      break;
    case 0b1100011:
      if (funct3 == 0b000) { // beq
        if (x[rs1] == x[rs2]) {
          printf(
              "0x%08x:beq    %s,%s,0x%08x   (0x%08x==0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);
          fprintf(
              files.output,
              "0x%08x:beq    %s,%s,0x%08x   (0x%08x==0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      } else if (funct3 == 0b001) { // bne
        if (x[rs1] != x[rs2]) {
          printf(
              "0x%08x:bne    %s,%s,0x%08x   (0x%08x!=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);
          fprintf(
              files.output,
              "0x%08x:bne    %s,%s,0x%08x   (0x%08x!=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      } else if (funct3 == 0b100) { // blt
        if ((int32_t)x[rs1] < (int32_t)x[rs2]) {

          uint32_t targetAddress = pc + branchImm;
          printf("DEBUG BLT: pc=0x%08x, branchImm=%d (0x%x), target=0x%08x\n",
                 pc, branchImm, branchImm, targetAddress);

          printf("0x%08x:blt    %s,%s,0x%08x   (0x%08x<0x%08x)=u1->pc=0x%08x\n",
                 pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
                 pc + branchImm);

          fprintf(
              files.output,
              "0x%08x:blt    %s,%s,0x%08x   (0x%08x<0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      } else if (funct3 == 0b101) { // bge
        if ((int32_t)x[rs1] >= (int32_t)x[rs2]) {
          printf(
              "0x%08x:bge    %s,%s,0x%08x   (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);
          fprintf(
              files.output,
              "0x%08x:bge    %s,%s,0x%08x   (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      } else if (funct3 == 0b110) { // bltu
        if (x[rs1] < x[rs2]) {
          printf(
              "0x%08x:bltu    %s,%s,0x%08x   (0x%08x<0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);
          fprintf(
              files.output,
              "0x%08x:bltu    %s,%s,0x%08x   (0x%08x<0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      } else if (funct3 == 0b111) { // bgeu
        if (x[rs1] >= x[rs2]) {
          printf(
              "0x%08x:bgeu    %s,%s,0x%08x   (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);
          fprintf(
              files.output,
              "0x%08x:bgeu    %s,%s,0x%08x   (0x%08x>=0x%08x)=u1->pc=0x%08x\n",
              pc, x_label[rs1], x_label[rs2], pc + branchImm, x[rs1], x[rs2],
              pc + branchImm);

          pc += branchImm - 4;
        }
      }
      break;
    case 0b1110011:
      // ebreak
      if (funct3 == 0b000 && imm == 1) {
        printf("0x%08x:ebreak\n", pc);
        fprintf(files.output, "0x%08x:ebreak\n", pc);
        run = 0;
      }
      break;
    case 0b1101111: { // jal
      const uint32_t simm = (imm20 >> 19) ? (0xFFF00000 | imm20) : (imm20);
      const uint32_t address = pc + (simm << 1);

      printf("0x%08x:jal    %s,0x%05x    pc=0x%08x,%s=0x%08x\n", pc,
             x_label[rd], imm, address, x_label[rd], pc + 4);

      fprintf(files.output, "0x%08x:jal    %s,0x%05x    pc=0x%08x,%s=0x%08x\n",
              pc, x_label[rd], imm, address, x_label[rd], pc + 4);

      if (rd != 0)
        x[rd] = pc + 4;
      pc = address - 4;
    } break;
    default:
      printf("error: unknown instruction opcode at pc = 0x%08x\n", pc);
      run = 0;
    }
    pc = pc + 4;
  }

  fclose(files.input);
  fclose(files.output);
  fclose(files.terminalInput);
  fclose(files.terminalOutput);

  return 0;
}
