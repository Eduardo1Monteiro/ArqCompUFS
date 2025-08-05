// Imports
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// MACROS
// Definições do Mapa de Memória para Dispositivos de E/S
#define CLINT_BASE 0x02000000
#define CLINT_MSIP (CLINT_BASE + 0x0)
#define CLINT_MTIMECMP (CLINT_BASE + 0x4000)
#define CLINT_MTIME (CLINT_BASE + 0xBFF8)
#define OFFSET 0x80000000

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
}

int32_t signedImmediate(uint16_t imm) {
  return (imm & 0x800) ? (0xFFFFF000 | imm) : imm;
}

void loadRd(uint32_t data, uint8_t rd, uint32_t x[]) {
  if (rd != 0) {
    x[rd] = data;
  }
}

void triggerException(uint32_t cause, uint32_t tval, uint32_t *pc,
                      uint32_t *mepc, uint32_t *mcause, uint32_t *mtvec,
                      uint32_t *mtval, uint32_t *mstatus) {
  *mepc = *pc;
  *mcause = cause;
  *mtval = tval;

  uint32_t mie = (*mstatus >> 3) & 1;
  *mstatus &= ~(1 << 7);
  *mstatus |= (mie << 7);
  *mstatus &= ~(1 << 3);

  if ((*mtvec & 0x1) && (cause & 0x80000000)) {
    uint32_t base = *mtvec & ~0x3;
    uint32_t cause_num = cause & 0x7FFFFFFF;
    *pc = base + (4 * cause_num);
  } else {
    *pc = *mtvec & ~0x3;
  }
}

const char *getCsrName(uint16_t address) {
  switch (address) {
  case 0x300:
    return "mstatus";
  case 0x304:
    return "mie";
  case 0x305:
    return "mtvec";
  case 0x341:
    return "mepc";
  case 0x342:
    return "mcause";
  case 0x343:
    return "mtval";
  case 0x344:
    return "mip";
  default:
    return "unknown_csr";
  }
}

uint32_t readCsr(uint16_t address, uint32_t mepc, uint32_t mcause,
                 uint32_t mtvec, uint32_t mtval, uint32_t mstatus, uint32_t mie,
                 uint32_t mip) {
  switch (address) {
  case 0x300:
    return mstatus;
  case 0x304:
    return mie;
  case 0x305:
    return mtvec;
  case 0x341:
    return mepc;
  case 0x342:
    return mcause;
  case 0x343:
    return mtval;
  case 0x344:
    return mip;
  default:
    return 0;
  }
}

void writeCsr(uint16_t address, uint32_t data, uint32_t *mepc, uint32_t *mcause,
              uint32_t *mtvec, uint32_t *mtval, uint32_t *mstatus,
              uint32_t *mie, uint32_t *mip) {
  switch (address) {
  case 0x300:
    *mstatus = data;
    break;
  case 0x304:
    *mie = data;
    break;
  case 0x305:
    *mtvec = data;
    break;
  case 0x341:
    *mepc = data;
    break;
  case 0x342:
    *mcause = data;
    break;
  case 0x343:
    *mtval = data;
    break;
  case 0x344:
    *mip = data;
    break;
  }
}

int main(int argc, char *argv[]) {

  printf("Code being executed...");

  Files files = readFile(argv);

  uint32_t pc = OFFSET;

  // registers
  uint32_t x[32] = {0};

  // registers name
  const char *x_label[32] = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

  uint8_t *mem = (uint8_t *)(malloc(32 * 1024));

  if (mem == NULL) {
    perror("FATAL: Failed to allocate memory");
    exit(EXIT_FAILURE);
  }

  loadMemory(files.input, OFFSET, mem);

  // VARIABLES SECTION
  uint8_t run = 1;
  // CSRs
  uint32_t mepc = 0, mcause = 0, mtvec = 0, mtval = 0, mstatus = 0, mie = 0,
           mip = 0;
  uint64_t mtime = 0, mtimecmp = 0;

  uint32_t clintMsip = 0;

  while (run) {
    if (clintMsip > 0) {
      mip |= (1 << 3);
    } else {
      mip &= ~(1 << 3);
    }

    // TIMER
    if (mtime >= mtimecmp) {
      mip |= (1 << 7); // MTIP
    } else {
      mip &= ~(1 << 7);
    }

    uint32_t pendingAndEnabled = mip & mie;
    uint32_t globalInterruptEnable = (mstatus >> 3) & 1;

    if (globalInterruptEnable && pendingAndEnabled != 0) {
      if (pendingAndEnabled & (1 << 7)) { // Timer Interrupt
        triggerException(0x80000007, 0, &pc, &mepc, &mcause, &mtvec, &mtval,
                         &mstatus);
        fprintf(files.output,
                ">interrupt:timer                         "
                "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                mcause, mepc, mtval);
        continue;
      }
      if (pendingAndEnabled & (1 << 3)) { // Software Interrupt
        triggerException(0x80000003, 0, &pc, &mepc, &mcause, &mtvec, &mtval,
                         &mstatus);
        fprintf(files.output,
                ">interrupt:software                      "
                "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                mcause, mepc, mtval);
        continue;
      }
    }

    if ((pc < OFFSET) || (pc >= (OFFSET + 32 * 1024 - 3))) {
      triggerException(1, pc, &pc, &mepc, &mcause, &mtvec, &mtval, &mstatus);
      fprintf(files.output,
              ">exception:instruction_fault           "
              "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
              mcause, mepc, mtval);
      continue;
    }

    uint32_t instruction = mem[pc - OFFSET] | (mem[pc - OFFSET + 1] << 8) |
                           (mem[pc - OFFSET + 2] << 16) |
                           (mem[pc - OFFSET + 3] << 24);
    const uint8_t opcode = instruction & 0x7F;
    const uint8_t funct7 = (instruction >> 25) & 0x7F;
    const uint16_t imm = instruction >> 20;
    const uint8_t uimm = (instruction & (0b11111 << 20)) >> 20;
    const uint8_t rs1 = (instruction >> 15) & 0x1F;
    const uint8_t rs2 = (instruction >> 20) & 0x1F;
    const uint8_t funct3 = (instruction >> 12) & 0x07;
    const uint8_t rd = (instruction >> 7) & 0x1F;
    const uint16_t immS =
        ((instruction >> 25) & 0x7F) << 5 | ((instruction >> 7) & 0x1F);

    const uint32_t imm12 = (instruction >> 19) & 0x1000;
    const uint32_t imm11 = (instruction & 0x80) << 4;
    const uint32_t imm10_5 = (instruction >> 20) & 0x7E0;
    const uint32_t imm4_1 = (instruction >> 7) & 0x1E;
    const uint32_t imm_b_unsigned = imm12 | imm11 | imm10_5 | imm4_1;
    const int32_t branchImm = (imm_b_unsigned & 0x1000)
                                  ? (0xFFFFE000 | imm_b_unsigned)
                                  : imm_b_unsigned;

    const uint32_t imm_j_unsigned = (((instruction >> 31) & 0x1) << 20) |
                                    (((instruction >> 12) & 0xFF) << 12) |
                                    (((instruction >> 20) & 0x1) << 11) |
                                    (((instruction >> 21) & 0x3FF) << 1);
    const int32_t jalOffset = (imm_j_unsigned & 0x100000)
                                  ? (0xFFE00000 | imm_j_unsigned)
                                  : imm_j_unsigned;

    switch (opcode) {
    case 0b0010011: // I-Type
      if (funct3 == 0b001 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] << uimm;
        fprintf(files.output,
                "0x%08x:slli   %s,%s,%u       %s=0x%08x<<%u=0x%08x\n", pc,
                x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
                data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b000) {
        const int32_t simm = signedImmediate(imm);
        const int32_t data = simm + ((int32_t)x[rs1]);
        fprintf(files.output,
                "0x%08x:addi   %s,%s,0x%03x         %s=0x%08x+0x%08x=0x%08x\n",
                pc, x_label[rd], x_label[rs1], imm, x_label[rd], x[rs1], simm,
                data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b111) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] & simm;
        fprintf(files.output,
                "0x%08x:andi   %s,%s,0x%03x   %s=0x%08x&0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], (uint32_t)(imm & 0xFFF), x_label[rd],
                x[rs1], simm, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b110) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] | simm;
        fprintf(files.output,
                "0x%08x:ori    %s,%s,0x%03x   %s=0x%08x|0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], (uint32_t)(imm & 0xFFF), x_label[rd],
                x[rs1], simm, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b100) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] ^ simm;
        fprintf(files.output,
                "0x%08x:xori   %s,%s,0x%03x   %s=0x%08x^0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], (uint32_t)(imm & 0xFFF), x_label[rd],
                x[rs1], simm, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b010) {
        int32_t simm = signedImmediate(imm);
        uint32_t data = ((int32_t)x[rs1] < simm) ? 1 : 0;
        fprintf(files.output,
                "0x%08x:slti   %s,%s,0x%03x   %s=(0x%08x<0x%08x)=%d\n", pc,
                x_label[rd], x_label[rs1], (uint32_t)(imm & 0xFFF), x_label[rd],
                x[rs1], simm, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b011) {
        uint32_t simm = signedImmediate(imm);
        uint32_t data = ((uint32_t)x[rs1] < (uint32_t)simm) ? 1 : 0;
        fprintf(files.output,
                "0x%08x:sltiu  %s,%s,0x%03x   %s=(0x%08x<0x%08x)=%d\n", pc,
                x_label[rd], x_label[rs1], (uint32_t)(imm & 0xFFF), x_label[rd],
                x[rs1], simm, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] >> uimm;
        fprintf(files.output, "0x%08x:srli   %s,%s,%d   %s=0x%08x>>%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
                data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0100000) {
        const uint32_t data = ((int32_t)x[rs1]) >> uimm;
        fprintf(files.output,
                "0x%08x:srai   %s,%s,%d   %s=0x%08x>>>%d=0x%08x\n", pc,
                x_label[rd], x_label[rs1], uimm, x_label[rd], x[rs1], uimm,
                data);
        loadRd(data, rd, x);
      }
      break;
    case 0b0110111: { // LUI
      uint32_t immU = instruction & 0xFFFFF000;
      uint32_t data = immU;
      fprintf(files.output, "0x%08x:lui    %s,0x%05x         %s=0x%08x\n", pc,
              x_label[rd], (immU >> 12), x_label[rd], data);
      loadRd(data, rd, x);
      break;
    }
    case 0b0010111: { // AUIPC
      uint32_t immU = instruction & 0xFFFFF000;
      uint32_t data = immU + pc;
      fprintf(files.output,
              "0x%08x:auipc  %s,0x%05x         %s=0x%08x+0x%08x=0x%08x\n", pc,
              x_label[rd], (immU >> 12), x_label[rd], pc, immU, data);
      loadRd(data, rd, x);
      break;
    }
    case 0b0000011: { // L-Type
      const int32_t simm = signedImmediate(imm);
      const uint32_t address = x[rs1] + simm;
      uint32_t data = 0;
      int handled = 1;

      if (address == CLINT_MSIP) {
        data = clintMsip;
      } else if (address == CLINT_MTIMECMP) {
        data = (uint32_t)mtimecmp;
      } else if (address == CLINT_MTIMECMP + 4) {
        data = (uint32_t)(mtimecmp >> 32);
      } else if (address == CLINT_MTIME) {
        data = (uint32_t)mtime;
      } else if (address == CLINT_MTIME + 4) {
        data = (uint32_t)(mtime >> 32);
      } else if (address >= OFFSET && address < (OFFSET + 32 * 1024)) {
        const uint32_t index = address - OFFSET;
        if (funct3 == 0b000) { // lb
          data = (int8_t)mem[index];
          fprintf(files.output,
                  "0x%08x:lb     %s,0x%03x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                  x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        } else if (funct3 == 0b001) { // lh
          data = (int16_t)(mem[index] | (mem[index + 1] << 8));
          fprintf(files.output,
                  "0x%08x:lh     %s,0x%03x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                  x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        } else if (funct3 == 0b100) { // lbu
          data = mem[index];
          fprintf(files.output,
                  "0x%08x:lbu    %s,0x%03x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                  x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        } else if (funct3 == 0b101) { // lhu
          data = mem[index] | (mem[index + 1] << 8);
          fprintf(files.output,
                  "0x%08x:lhu    %s,0x%03x(%s)   %s=mem[0x%08x]=0x%08x\n", pc,
                  x_label[rd], imm, x_label[rs1], x_label[rd], address, data);
        } else if (funct3 == 0b010) { // lw
          data = mem[index] | (mem[index + 1] << 8) | (mem[index + 2] << 16) |
                 (mem[index + 3] << 24);
          fprintf(files.output,
                  "0x%08x:lw     %s,0x%03x(%s)       %s=mem[0x%08x]=0x%08x\n",
                  pc, x_label[rd], imm, x_label[rs1], x_label[rd], address,
                  data);
        } else {
          handled = 0;
        }
      } else {
        handled = 0;
      }

      if (handled) {
        loadRd(data, rd, x);
      } else {
        triggerException(5, address, &pc, &mepc, &mcause, &mtvec, &mtval,
                         &mstatus);
        fprintf(files.output,
                ">exception:load_fault                     "
                "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                mcause, mepc, mtval);
        continue;
      }
      break;
    }
    case 0b1100111: { // JALR
      if (funct3 == 0b000) {
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = pc + 4;
        uint32_t address = x[rs1] + simm;
        address = address & ~1;
        fprintf(files.output,
                "0x%08x:jalr   %s,%s,0x%03x       pc=0x%08x+0x%08x,%s=0x%08x\n",
                pc, x_label[rd], x_label[rs1], imm, x[rs1], simm, x_label[rd],
                data);
        loadRd(data, rd, x);
        pc = address - 4;
      }
      break;
    }
    case 0b0100011: { // S-Type
      const int32_t simm = signedImmediate(immS);
      const uint32_t address = x[rs1] + simm;
      const uint32_t data = x[rs2];
      int handled = 1;

      if (address == CLINT_MSIP) {
        clintMsip = data & 0x1;
        fprintf(files.output,
                "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], immS, x_label[rs1], address, data);
      } else if (address == CLINT_MTIMECMP) {
        mtimecmp = (mtimecmp & 0xFFFFFFFF00000000) | data;
        fprintf(files.output,
                "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], immS, x_label[rs1], address, data);
      } else if (address == CLINT_MTIMECMP + 4) {
        mtimecmp = (mtimecmp & 0x00000000FFFFFFFF) | ((uint64_t)data << 32);
        fprintf(files.output,
                "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], immS, x_label[rs1], address, data);
      } else if (address == CLINT_MTIME) {
        mtime = (mtime & 0xFFFFFFFF00000000) | data;
        fprintf(files.output,
                "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], immS, x_label[rs1], address, data);
      } else if (address == CLINT_MTIME + 4) {
        mtime = (mtime & 0x00000000FFFFFFFF) | ((uint64_t)data << 32);
        fprintf(files.output,
                "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                x_label[rs2], immS, x_label[rs1], address, data);
      } else if (address >= OFFSET && address < (OFFSET + 32 * 1024)) {
        const uint32_t index = address - OFFSET;
        if (funct3 == 0b010) { // sw
          mem[index + 0] = (data >> 0) & 0xFF;
          mem[index + 1] = (data >> 8) & 0xFF;
          mem[index + 2] = (data >> 16) & 0xFF;
          mem[index + 3] = (data >> 24) & 0xFF;
          fprintf(files.output,
                  "0x%08x:sw     %s,0x%03x(%s)       mem[0x%08x]=0x%08x\n", pc,
                  x_label[rs2], immS, x_label[rs1], address, data);
        } else if (funct3 == 0b001) { // sh
          mem[index + 0] = (data >> 0) & 0xFF;
          mem[index + 1] = (data >> 8) & 0xFF;
          fprintf(files.output,
                  "0x%08x:sh     %s,0x%03x(%s)   mem[0x%08x]=0x%04x\n", pc,
                  x_label[rs2], immS, x_label[rs1], address,
                  (uint32_t)(data & 0xFFFF));
        } else if (funct3 == 0b000) { // sb
          mem[index + 0] = (data >> 0) & 0xFF;
          fprintf(files.output,
                  "0x%08x:sb     %s,0x%03x(%s)   mem[0x%08x]=0x%02x\n", pc,
                  x_label[rs2], immS, x_label[rs1], address,
                  (uint32_t)(data & 0xFF));
        } else {
          handled = 0;
        }
      } else {
        handled = 0;
      }

      if (!handled) {
        triggerException(7, address, &pc, &mepc, &mcause, &mtvec, &mtval,
                         &mstatus);
        fprintf(files.output,
                ">exception:store_fault                    "
                "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                mcause, mepc, mtval);
        continue;
      }
      break;
    }
    case 0b0110011: // R-Type
      if (funct3 == 0b000 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] + x[rs2];
        fprintf(files.output,
                "0x%08x:add    %s,%s,%s         %s=0x%08x+0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b000 && funct7 == 0b0100000) {
        const uint32_t data = x[rs1] - x[rs2];
        fprintf(files.output,
                "0x%08x:sub    %s,%s,%s   %s=0x%08x-0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b100 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] ^ x[rs2];
        fprintf(files.output,
                "0x%08x:xor    %s,%s,%s   %s=0x%08x^0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b110 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] | x[rs2];
        fprintf(files.output,
                "0x%08x:or     %s,%s,%s   %s=0x%08x|0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b111 && funct7 == 0b0000000) {
        const uint32_t data = x[rs1] & x[rs2];
        fprintf(files.output,
                "0x%08x:and    %s,%s,%s   %s=0x%08x&0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b010 && funct7 == 0b0000000) {
        const uint32_t data = ((int32_t)x[rs1] < (int32_t)x[rs2]) ? 1 : 0;
        fprintf(files.output,
                "0x%08x:slt    %s,%s,%s   %s=(0x%08x<0x%08x)=%d\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b011 && funct7 == 0b0000000) {
        const uint32_t data = ((uint32_t)x[rs1] < (uint32_t)x[rs2]) ? 1 : 0;
        fprintf(files.output,
                "0x%08x:sltu   %s,%s,%s   %s=(0x%08x<0x%08x)=%d\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b001 && funct7 == 0b0000000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const uint32_t data = x[rs1] << shift;
        fprintf(files.output, "0x%08x:sll    %s,%s,%s   %s=0x%08x<<%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd],
                x[rs1], shift, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const uint32_t data = x[rs1] >> shift;
        fprintf(files.output, "0x%08x:srl    %s,%s,%s   %s=0x%08x>>%d=0x%08x\n",
                pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd],
                x[rs1], shift, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0100000) {
        const uint32_t shift = x[rs2] & 0b11111;
        const int32_t data = ((int32_t)x[rs1]) >> shift;
        fprintf(files.output,
                "0x%08x:sra    %s,%s,%s   %s=0x%08x>>>%d=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                shift, data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b000 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const int64_t v2 = (int32_t)x[rs2];
        const int64_t product = v1 * v2;
        const uint32_t data = product;
        fprintf(files.output,
                "0x%08x:mul    %s,%s,%s           %s=0x%08x*0x%08x=0x%08x\n",
                pc, x_label[rd], x_label[rs1], x_label[rs2], x_label[rd],
                x[rs1], x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b001 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const int64_t v2 = (int32_t)x[rs2];
        const int64_t product = (int64_t)v1 * (int64_t)v2;
        const uint32_t data = product >> 32;
        fprintf(files.output,
                "0x%08x:mulh   %s,%s,%s   %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b010 && funct7 == 0b0000001) {
        const int64_t v1 = (int32_t)x[rs1];
        const uint64_t v2 = (uint32_t)x[rs2];
        const int64_t product = (int64_t)v1 * (uint64_t)v2;
        const uint32_t data = product >> 32;
        fprintf(files.output,
                "0x%08x:mulhsu %s,%s,%s   %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b011 && funct7 == 0b0000001) {
        const uint64_t v1 = (uint32_t)x[rs1];
        const uint64_t v2 = (uint32_t)x[rs2];
        const uint64_t product = (uint64_t)v1 * (uint64_t)v2;
        const uint32_t data = product >> 32;
        fprintf(files.output,
                "0x%08x:mulhu  %s,%s,%s   %s=0x%08x*0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b100 && funct7 == 0b0000001) {
        int32_t data;
        if (x[rs2] == 0) {
          data = 0xFFFFFFFF;
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0x80000000;
        } else {
          data = (int32_t)x[rs1] / (int32_t)x[rs2];
        }
        fprintf(files.output,
                "0x%08x:div    %s,%s,%s   %s=0x%08x/0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000001) {
        uint32_t data;
        if (x[rs2] == 0) {
          data = 0xFFFFFFFF;
        } else {
          data = (uint32_t)x[rs1] / (uint32_t)x[rs2];
        }
        fprintf(files.output,
                "0x%08x:divu   %s,%s,%s   %s=0x%08x/0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b110 && funct7 == 0b0000001) {
        int32_t data;
        if (x[rs2] == 0) {
          data = x[rs1];
        } else if (x[rs1] == 0x80000000 && x[rs2] == -1) {
          data = 0;
        } else {
          data = (int32_t)x[rs1] % (int32_t)x[rs2];
        }
        fprintf(files.output,
                "0x%08x:rem    %s,%s,%s   %s=0x%08x%%0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      } else if (funct3 == 0b111 && funct7 == 0b0000001) {
        uint32_t data;
        if (x[rs2] == 0) {
          data = x[rs1];
        } else {
          data = (uint32_t)x[rs1] % (uint32_t)x[rs2];
        }
        fprintf(files.output,
                "0x%08x:remu   %s,%s,%s   %s=0x%08x%%0x%08x=0x%08x\n", pc,
                x_label[rd], x_label[rs1], x_label[rs2], x_label[rd], x[rs1],
                x[rs2], data);
        loadRd(data, rd, x);
      }
      break;
    case 0b1100011: { // B-Type
      uint32_t nextPc = pc + 4;
      if (funct3 == 0b000) { // beq
        if (x[rs1] == x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:beq    %s,%s,0x%03x      (0x%08x==0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], x[rs1] == x[rs2], nextPc);
      } else if (funct3 == 0b001) { // bne
        if (x[rs1] != x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:bne    %s,%s,0x%03x      (0x%08x!=0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], x[rs1] != x[rs2], nextPc);
      } else if (funct3 == 0b100) { // blt
        if ((int32_t)x[rs1] < (int32_t)x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:blt    %s,%s,0x%03x      (0x%08x<0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], (int32_t)x[rs1] < (int32_t)x[rs2], nextPc);
      } else if (funct3 == 0b101) { // bge
        if ((int32_t)x[rs1] >= (int32_t)x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:bge    %s,%s,0x%03x      (0x%08x>=0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], (int32_t)x[rs1] >= (int32_t)x[rs2], nextPc);
      } else if (funct3 == 0b110) { // bltu
        if (x[rs1] < x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:bltu   %s,%s,0x%03x      (0x%08x<0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], x[rs1] < x[rs2], nextPc);
      } else if (funct3 == 0b111) { // bgeu
        if (x[rs1] >= x[rs2]) {
          nextPc = pc + branchImm;
        }
        fprintf(
            files.output,
            "0x%08x:bgeu   %s,%s,0x%03x      (0x%08x>=0x%08x)=%d->pc=0x%08x\n",
            pc, x_label[rs1], x_label[rs2], (uint32_t)(branchImm >> 1), x[rs1],
            x[rs2], x[rs1] >= x[rs2], nextPc);
      }
      if (nextPc != pc + 4) {
        pc = nextPc - 4;
      }
      break;
    }
    case 0b1101111: { // JAL
      const uint32_t data = pc + 4;
      const uint32_t address = pc + jalOffset;

      fprintf(files.output,
              "0x%08x:jal    %s,0x%05x       pc=0x%08x,%s=0x%08x\n", pc,
              x_label[rd], (uint32_t)(jalOffset / 2) & 0xFFFFF, address,
              x_label[rd], data);

      loadRd(data, rd, x);
      pc = address - 4;
      break;
    }
    case 0b1110011: { // SYSTEM
      const uint16_t csrAddress = imm;
      if (funct3 == 0b000 && csrAddress == 0) {
        triggerException(11, pc, &pc, &mepc, &mcause, &mtvec, &mtval, &mstatus);
        fprintf(files.output,
                ">exception:environment_call             "
                "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                mcause, mepc, mtval);
        continue;
      } else if (funct3 == 0b000 && csrAddress == 0x302) {
        uint32_t mpie = (mstatus >> 7) & 1;
        mstatus &= ~(1 << 3);
        mstatus |= (mpie << 3);
        mstatus |= (1 << 7);
        fprintf(files.output,
                "0x%08x:mret                                 pc=0x%08x\n", pc,
                mepc);
        pc = mepc;
        continue;
      } else if (funct3 == 0b000 && csrAddress == 1) {
        fprintf(files.output, "0x%08x:ebreak\n", pc);
        run = 0;
      } else {
        uint32_t oldCsrValue =
            readCsr(csrAddress, mepc, mcause, mtvec, mtval, mstatus, mie, mip);
        uint32_t newCsrValue;

        switch (funct3) {
        case 0b001: // CSRRW
          newCsrValue = x[rs1];
          writeCsr(csrAddress, newCsrValue, &mepc, &mcause, &mtvec, &mtval,
                   &mstatus, &mie, &mip);
          loadRd(oldCsrValue, rd, x);
          fprintf(files.output,
                  "0x%08x:csrrw  %s,%s,%s       %s=%s=0x%08x,%s=%s=0x%08x\n",
                  pc, x_label[rd], getCsrName(csrAddress), x_label[rs1],
                  x_label[rd], getCsrName(csrAddress), oldCsrValue,
                  getCsrName(csrAddress), x_label[rs1], newCsrValue);
          break;
        case 0b010: // CSRRS
          newCsrValue = oldCsrValue | x[rs1];
          loadRd(oldCsrValue, rd, x);
          fprintf(files.output,
                  "0x%08x:csrrs  %s,%s,%s       "
                  "%s=%s=0x%08x,%s|=%s=0x%08x|0x%08x=0x%08x\n",
                  pc, x_label[rd], getCsrName(csrAddress), x_label[rs1],
                  x_label[rd], getCsrName(csrAddress), oldCsrValue,
                  getCsrName(csrAddress), x_label[rs1], oldCsrValue, x[rs1],
                  newCsrValue);
          if (rs1 != 0) {
            writeCsr(csrAddress, newCsrValue, &mepc, &mcause, &mtvec, &mtval,
                     &mstatus, &mie, &mip);
          }
          break;
        default:
          triggerException(2, instruction, &pc, &mepc, &mcause, &mtvec, &mtval,
                           &mstatus);
          fprintf(files.output,
                  ">exception:illegal_instruction          "
                  "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
                  mcause, mepc, mtval);
          continue;
        }
      }
      break;
    }
    default:
      triggerException(2, instruction, &pc, &mepc, &mcause, &mtvec, &mtval,
                       &mstatus);
      fprintf(files.output,
              ">exception:illegal_instruction          "
              "cause=0x%08x,epc=0x%08x,tval=0x%08x\n",
              mcause, mepc, mtval);
      continue;
    }
    pc += 4;
    mtime++;
  }

  fclose(files.input);
  fclose(files.output);
  fclose(files.terminalInput);
  fclose(files.terminalOutput);
  free(mem);

  return 0;
}
