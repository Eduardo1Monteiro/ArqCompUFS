#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace MemoryMap {
constexpr uint32_t OFFSET = 0x80000000;
constexpr uint32_t CLINT_BASE = 0x02000000;
constexpr uint32_t CLINT_MSIP = CLINT_BASE + 0x0;
constexpr uint32_t CLINT_MTIMECMP = CLINT_BASE + 0x4000;
constexpr uint32_t CLINT_MTIME = CLINT_BASE + 0xBFF8;
constexpr uint32_t UART_BASE = 0x10000000;
constexpr uint32_t UART_TX_REG = UART_BASE + 0x0;
constexpr uint32_t PLIC_BASE = 0x0c000000;
constexpr uint32_t PLIC_PENDING = PLIC_BASE + 0x1000;
constexpr uint32_t PLIC_ENABLE = PLIC_BASE + 0x2000;
constexpr uint32_t PLIC_THRESHOLD = 0x0c200000;
constexpr uint32_t PLIC_CLAIM = 0x0c200004;
constexpr uint32_t UART_IRQ = 10;
} // namespace MemoryMap

class Files {
public:
  std::ifstream input;
  std::ofstream output;
  std::ifstream terminalInput;
  std::ofstream terminalOutput;

  Files(int argc, char *argv[]) {
    if (argc < 5) {
      std::cerr << "Usage: " << argv[0]
                << " <input_file> <output_file> <terminal_in> <terminal_out>"
                << std::endl;
      exit(EXIT_FAILURE);
    }
    input.open(argv[1]);
    output.open(argv[2]);
    terminalInput.open(argv[3]);
    terminalOutput.open(argv[4]);

    if (!input.is_open() || !output.is_open() || !terminalInput.is_open() ||
        !terminalOutput.is_open()) {
      std::cerr << "FATAL: Failed to open one or more files." << std::endl;
      exit(EXIT_FAILURE);
    }
  }

  ~Files() {
    if (input.is_open())
      input.close();
    if (output.is_open())
      output.close();
    if (terminalInput.is_open())
      terminalInput.close();
    if (terminalOutput.is_open())
      terminalOutput.close();
  }
};

std::string hex_format(uint32_t val, int width) {
  std::stringstream ss;
  ss << "0x" << std::hex << std::setw(width) << std::setfill('0') << val;
  return ss.str();
}

class CacheLine {
public:
  bool isValid = false;
  uint32_t tag = 0;
  std::array<uint32_t, 4> block;
  uint8_t lruCounter = 0;
};

class Cache {
private:
  std::vector<std::vector<CacheLine>> sets;
  const unsigned int numSets = 8;
  const unsigned int associativity = 2;
  uint64_t hits = 0;
  uint64_t misses = 0;
  std::string cacheType;
  std::ofstream &output;

  unsigned int findLRU(unsigned int setIndex) {
    if (!sets[setIndex][0].isValid)
      return 0;
    if (!sets[setIndex][1].isValid)
      return 1;
    return (sets[setIndex][0].lruCounter > sets[setIndex][1].lruCounter) ? 0
                                                                         : 1;
  }

  void updateLRU(unsigned int setIndex, unsigned int accessedWay) {
    sets[setIndex][accessedWay].lruCounter = 0;
    sets[setIndex][1 - accessedWay].lruCounter++;
  }

public:
  Cache(const std::string &name, std::ofstream &out)
      : cacheType(name), output(out) {
    sets.resize(numSets, std::vector<CacheLine>(associativity));
  }

  uint32_t read(uint32_t address, std::vector<uint8_t> &mem) {
    uint32_t offset = (address & 0xF) >> 2;
    uint32_t index = (address >> 4) & 0x7;
    uint32_t tag = address >> 7;

    for (unsigned int i = 0; i < associativity; ++i) {
      if (sets[index][i].isValid && sets[index][i].tag == tag) {
        hits++;
        output << "#cache_mem:" << cacheType << "rh " << hex_format(address, 8)
               << "       line=" << index
               << ",age=" << static_cast<int>(sets[index][i].lruCounter)
               << ",id=0x" << std::hex << std::setw(6) << std::setfill('0')
               << tag << ",block[" << i << "]={"
               << hex_format(sets[index][i].block[0], 8) << ","
               << hex_format(sets[index][i].block[1], 8) << ","
               << hex_format(sets[index][i].block[2], 8) << ","
               << hex_format(sets[index][i].block[3], 8) << "}" << std::dec
               << std::endl;
        updateLRU(index, i);
        return sets[index][i].block[offset];
      }
    }

    misses++;
    unsigned int victimWay = findLRU(index);
    CacheLine &victimLine = sets[index][victimWay];
    output << "#cache_mem:" << cacheType << "rm " << hex_format(address, 8)
           << "       line=" << index << ",valid={" << sets[index][0].isValid
           << "," << sets[index][1].isValid << "},age={"
           << static_cast<int>(sets[index][1].lruCounter) << ","
           << static_cast<int>(sets[index][0].lruCounter) << "},id={0x"
           << std::hex << std::setw(6) << std::setfill('0')
           << sets[index][0].tag << ",0x" << std::setw(6) << std::setfill('0')
           << sets[index][1].tag << "}" << std::dec << std::endl;

    victimLine.isValid = true;
    victimLine.tag = tag;
    uint32_t blockStartAddr = address & ~0xF;
    for (int i = 0; i < 4; ++i) {
      uint32_t memAddr = blockStartAddr + (i * 4);
      uint32_t memIndex = memAddr - MemoryMap::OFFSET;
      if (memIndex + 3 < mem.size()) {
        victimLine.block[i] =
            (mem[memIndex + 0] << 0) | (mem[memIndex + 1] << 8) |
            (mem[memIndex + 2] << 16) | (mem[memIndex + 3] << 24);
      }
    }
    updateLRU(index, victimWay);
    return victimLine.block[offset];
  }

  void write(uint32_t address, uint32_t data, uint8_t funct3,
             std::vector<uint8_t> &mem) {
    uint32_t offset = (address & 0xF) >> 2;
    uint32_t index = (address >> 4) & 0x7;
    uint32_t tag = address >> 7;
    uint32_t byte_offset = address & 0x3;

    for (unsigned int i = 0; i < associativity; ++i) {
      if (sets[index][i].isValid && sets[index][i].tag == tag) {
        hits++;
        output << "#cache_mem:" << cacheType << "wh " << hex_format(address, 8)
               << "       line=" << index
               << ",age=" << static_cast<int>(sets[index][i].lruCounter)
               << ",id=0x" << std::hex << std::setw(6) << std::setfill('0')
               << tag << ",block[" << i << "]={"
               << hex_format(sets[index][i].block[0], 8) << ","
               << hex_format(sets[index][i].block[1], 8) << ","
               << hex_format(sets[index][i].block[2], 8) << ","
               << hex_format(sets[index][i].block[3], 8) << "}" << std::dec
               << std::endl;

        uint32_t memIndex = address - MemoryMap::OFFSET;
        if (funct3 == 0b000) {
          uint32_t mask = ~(0xFF << (byte_offset * 8));
          sets[index][i].block[offset] = (sets[index][i].block[offset] & mask) |
                                         ((data & 0xFF) << (byte_offset * 8));
          mem[memIndex] = data & 0xFF;
        } else if (funct3 == 0b001) {
          uint32_t mask = ~(0xFFFF << (byte_offset * 8));
          sets[index][i].block[offset] = (sets[index][i].block[offset] & mask) |
                                         ((data & 0xFFFF) << (byte_offset * 8));
          mem[memIndex] = data & 0xFF;
          mem[memIndex + 1] = (data >> 8) & 0xFF;
        } else if (funct3 == 0b010) {
          sets[index][i].block[offset] = data;
          mem[memIndex] = data & 0xFF;
          mem[memIndex + 1] = (data >> 8) & 0xFF;
          mem[memIndex + 2] = (data >> 16) & 0xFF;
          mem[memIndex + 3] = (data >> 24) & 0xFF;
        }
        updateLRU(index, i);
        return;
      }
    }

    misses++;
    output << "#cache_mem:" << cacheType << "wm " << hex_format(address, 8)
           << "       line=" << index << ",valid={" << sets[index][0].isValid
           << "," << sets[index][1].isValid << "},age={"
           << static_cast<int>(sets[index][1].lruCounter) << ","
           << static_cast<int>(sets[index][0].lruCounter) << "},id={0x"
           << std::hex << std::setw(6) << std::setfill('0')
           << sets[index][0].tag << ",0x" << std::setw(6) << std::setfill('0')
           << sets[index][1].tag << "}" << std::dec << std::endl;

    uint32_t memIndex = address - MemoryMap::OFFSET;
    if (funct3 == 0b000) {
      mem[memIndex] = data & 0xFF;
    } else if (funct3 == 0b001) {
      mem[memIndex] = data & 0xFF;
      mem[memIndex + 1] = (data >> 8) & 0xFF;
    } else if (funct3 == 0b010) {
      mem[memIndex] = data & 0xFF;
      mem[memIndex + 1] = (data >> 8) & 0xFF;
      mem[memIndex + 2] = (data >> 16) & 0xFF;
      mem[memIndex + 3] = (data >> 24) & 0xFF;
    }
  }

  void printStats() {
    uint64_t totalAccesses = hits + misses;
    double hitRate =
        (totalAccesses == 0) ? 0.0 : static_cast<double>(hits) / totalAccesses;
    output << "#cache_mem:" << cacheType
           << "stats                hit=" << std::fixed << std::setprecision(4)
           << hitRate << std::endl;
  }
};

void loadMemory(std::ifstream &input, uint32_t offset,
                std::vector<uint8_t> &mem) {
  std::string lineBuffer;
  uint32_t currentAddress = 0;
  input.clear();
  input.seekg(0, std::ios::beg);

  while (std::getline(input, lineBuffer)) {
    if (lineBuffer.empty())
      continue;

    if (lineBuffer[0] == '@') {
      sscanf(lineBuffer.c_str(), "@%x", &currentAddress);
    } else {
      std::stringstream ss(lineBuffer);
      unsigned int byteVal = 0;
      while (ss >> std::hex >> byteVal) {
        if (currentAddress >= offset) {
          uint32_t memIndex = currentAddress - offset;
          if (memIndex < mem.size()) {
            mem[memIndex] = static_cast<uint8_t>(byteVal);
          }
        }
        currentAddress++;
      }
    }
  }
}

int32_t signedImmediate(uint16_t imm) {
  return (imm & 0x800) ? (0xFFFFF000 | imm) : imm;
}

void loadRd(uint32_t data, uint8_t rd, std::array<uint32_t, 32> &x) {
  if (rd != 0) {
    x[rd] = data;
  }
}

void triggerException(uint32_t cause, uint32_t tval, uint32_t &pc,
                      uint32_t &mepc, uint32_t &mcause, uint32_t &mtvec,
                      uint32_t &mtval, uint32_t &mstatus) {
  mepc = pc;
  mcause = cause;
  mtval = tval;

  uint32_t mie = (mstatus >> 3) & 1;
  mstatus &= ~(1 << 7);
  mstatus |= (mie << 7);
  mstatus &= ~(1 << 3);
  mstatus &= ~(0b11 << 11);
  mstatus |= (0b11 << 11);

  if ((mtvec & 0x1) && (cause & 0x80000000)) {
    uint32_t base = mtvec & ~0x3;
    uint32_t cause_num = cause & 0x7FFFFFFF;
    pc = base + (4 * cause_num);
  } else {
    pc = mtvec & ~0x3;
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

void writeCsr(uint16_t address, uint32_t data, uint32_t &mepc, uint32_t &mcause,
              uint32_t &mtvec, uint32_t &mtval, uint32_t &mstatus,
              uint32_t &mie, uint32_t &mip) {
  switch (address) {
  case 0x300:
    mstatus = data;
    break;
  case 0x304:
    mie = data;
    break;
  case 0x305:
    mtvec = data;
    break;
  case 0x341:
    mepc = data;
    break;
  case 0x342:
    mcause = data;
    break;
  case 0x343:
    mtval = data;
    break;
  case 0x344:
    mip = data;
    break;
  }
}

int main(int argc, char *argv[]) {
  std::cout << "Code being executed..." << std::endl;

  Files files(argc, argv);

  uint32_t pc = MemoryMap::OFFSET;
  std::array<uint32_t, 32> x = {0};

  const std::array<const char *, 32> x_label = {
      "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
      "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
      "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

  std::vector<uint8_t> mem(32 * 1024);
  loadMemory(files.input, MemoryMap::OFFSET, mem);

  Cache iCache("i", files.output);
  Cache dCache("d", files.output);

  bool run = true;
  uint32_t mepc = 0, mcause = 0, mtvec = 0, mtval = 0, mstatus = 0, mie = 0,
           mip = 0;
  uint64_t mtime = 0, mtimecmp = 0;
  uint32_t clintMsip = 0;
  uint32_t plicPendingReg = 0, plicEnableReg = 0, plicThresholdReg = 0;

  while (run) {
    if (clintMsip > 0) {
      mip |= (1 << 3);
    } else {
      mip &= ~(1 << 3);
    }

    if (mtime >= mtimecmp) {
      mip |= (1 << 7);
    } else {
      mip &= ~(1 << 7);
    }

    uint32_t plicPendingAndEnabled = plicPendingReg & plicEnableReg;
    if (plicPendingAndEnabled) {
      mip |= (1 << 11);
    } else {
      mip &= ~(1 << 11);
    }

    uint32_t pendingAndEnabled = mip & mie;
    uint32_t globalInterruptEnable = (mstatus >> 3) & 1;

    if (globalInterruptEnable && pendingAndEnabled != 0) {
      if (pendingAndEnabled & (1 << 11)) { // External Interrupt
        triggerException(0x8000000b, 0, pc, mepc, mcause, mtvec, mtval,
                         mstatus);
        files.output << ">interrupt:external              cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      }
      if (pendingAndEnabled & (1 << 7)) { // Timer Interrupt
        triggerException(0x80000007, 0, pc, mepc, mcause, mtvec, mtval,
                         mstatus);
        files.output << ">interrupt:timer                 cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      }
      if (pendingAndEnabled & (1 << 3)) { // Software Interrupt
        triggerException(0x80000003, 0, pc, mepc, mcause, mtvec, mtval,
                         mstatus);
        files.output << ">interrupt:software              cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      }
    }

    if ((pc < MemoryMap::OFFSET) ||
        (pc >= (MemoryMap::OFFSET + mem.size() - 3))) {
      triggerException(1, pc, pc, mepc, mcause, mtvec, mtval, mstatus);
      files.output << ">exception:instruction_fault     cause="
                   << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                   << ",tval=" << hex_format(pc, 8) << std::endl;
      continue;
    }

    uint32_t instruction = iCache.read(pc, mem);
    const uint8_t opcode = instruction & 0x7F;
    const uint8_t funct7 = (instruction >> 25) & 0x7F;
    const uint16_t imm = instruction >> 20;
    const uint8_t uimm = (instruction >> 20) & 0x1F;
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
    case 0b0010011:                                 // I-Type
      if (funct3 == 0b001 && funct7 == 0b0000000) { // slli
        const uint32_t data = x[rs1] << uimm;
        files.output << hex_format(pc, 8) << ":slli   " << x_label[rd] << ","
                     << x_label[rs1] << "," << std::dec
                     << static_cast<unsigned int>(uimm) << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "<<"
                     << std::dec << static_cast<unsigned int>(uimm) << "="
                     << hex_format(data, 8) << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b000) { // addi
        const int32_t simm = signedImmediate(imm);
        const int32_t data = simm + static_cast<int32_t>(x[rs1]);
        files.output << hex_format(pc, 8) << ":addi   " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "       " << x_label[rd] << "=" << hex_format(x[rs1], 8)
                     << "+" << hex_format(simm, 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b111) { // andi
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] & simm;
        files.output << hex_format(pc, 8) << ":andi   " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "       " << x_label[rd] << "=" << hex_format(x[rs1], 8)
                     << "&" << hex_format(simm, 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b110) { // ori
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] | simm;
        files.output << hex_format(pc, 8) << ":ori    " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "       " << x_label[rd] << "=" << hex_format(x[rs1], 8)
                     << "|" << hex_format(simm, 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b100) { // xori
        const uint32_t simm = signedImmediate(imm);
        const uint32_t data = x[rs1] ^ simm;
        files.output << hex_format(pc, 8) << ":xori   " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "       " << x_label[rd] << "=" << hex_format(x[rs1], 8)
                     << "^" << hex_format(simm, 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b010) { // slti
        int32_t simm = signedImmediate(imm);
        uint32_t data = (static_cast<int32_t>(x[rs1]) < simm) ? 1 : 0;
        files.output << hex_format(pc, 8) << ":slti   " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "     " << x_label[rd] << "=(" << hex_format(x[rs1], 8)
                     << "<" << hex_format(simm, 8) << ")=" << std::dec << data
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b011) { // sltiu
        uint32_t simm = signedImmediate(imm);
        uint32_t data = (x[rs1] < simm) ? 1 : 0;
        files.output << hex_format(pc, 8) << ":sltiu  " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "     " << x_label[rd] << "=(" << hex_format(x[rs1], 8)
                     << "<" << hex_format(simm, 8) << ")=" << std::dec << data
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000000) { // srli
        const uint32_t data = x[rs1] >> uimm;
        files.output << hex_format(pc, 8) << ":srli   " << x_label[rd] << ","
                     << x_label[rs1] << "," << std::dec
                     << static_cast<unsigned int>(uimm) << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << ">>"
                     << std::dec << static_cast<unsigned int>(uimm) << "="
                     << hex_format(data, 8) << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0100000) { // srai
        const uint32_t data = static_cast<int32_t>(x[rs1]) >> uimm;
        files.output << hex_format(pc, 8) << ":srai   " << x_label[rd] << ","
                     << x_label[rs1] << "," << std::dec
                     << static_cast<unsigned int>(uimm) << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << ">>>"
                     << std::dec << static_cast<unsigned int>(uimm) << "="
                     << hex_format(data, 8) << std::endl;
        loadRd(data, rd, x);
      }
      break;
    case 0b0110111: { // lui
      uint32_t immU = instruction & 0xFFFFF000;
      uint32_t data = immU;
      files.output << hex_format(pc, 8) << ":lui    " << x_label[rd] << ","
                   << hex_format(immU >> 12, 5) << "         " << x_label[rd]
                   << "=" << hex_format(data, 8) << std::endl;
      loadRd(data, rd, x);
      break;
    }
    case 0b0010111: { // auipc
      uint32_t immU = instruction & 0xFFFFF000;
      uint32_t data = immU + pc;
      files.output << hex_format(pc, 8) << ":auipc  " << x_label[rd] << ","
                   << hex_format(immU >> 12, 5) << "       " << x_label[rd]
                   << "=" << hex_format(pc, 8) << "+" << hex_format(immU, 8)
                   << "=" << hex_format(data, 8) << std::endl;
      loadRd(data, rd, x);
      break;
    }
    case 0b0000011: { // L-Type
      const int32_t simm = signedImmediate(imm);
      const uint32_t address = x[rs1] + simm;
      uint32_t data = 0;
      bool handled = true;

      if (address >= MemoryMap::OFFSET &&
          address < (MemoryMap::OFFSET + mem.size())) {
        uint32_t wordData = dCache.read(address, mem);
        uint32_t byteOffset = address & 0x3;

        if (funct3 == 0b000) { // lb
          data = static_cast<int8_t>(wordData >> (byteOffset * 8));
          files.output << hex_format(pc, 8) << ":lb     " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        } else if (funct3 == 0b001) { // lh
          data = static_cast<int16_t>(wordData >> (byteOffset * 8));
          files.output << hex_format(pc, 8) << ":lh     " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        } else if (funct3 == 0b010) { // lw
          data = wordData;
          files.output << hex_format(pc, 8) << ":lw     " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        } else if (funct3 == 0b100) { // lbu
          data = (wordData >> (byteOffset * 8)) & 0xFF;
          files.output << hex_format(pc, 8) << ":lbu    " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        } else if (funct3 == 0b101) { // lhu
          data = (wordData >> (byteOffset * 8)) & 0xFFFF;
          files.output << hex_format(pc, 8) << ":lhu    " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        } else {
          handled = false;
        }
      } else {
        if (address == MemoryMap::CLINT_MSIP) {
          data = clintMsip;
        } else if (address == MemoryMap::CLINT_MTIMECMP) {
          data = static_cast<uint32_t>(mtimecmp);
        } else if (address == MemoryMap::CLINT_MTIMECMP + 4) {
          data = static_cast<uint32_t>(mtimecmp >> 32);
        } else if (address == MemoryMap::CLINT_MTIME) {
          data = static_cast<uint32_t>(mtime);
        } else if (address == MemoryMap::CLINT_MTIME + 4) {
          data = static_cast<uint32_t>(mtime >> 32);
        } else if (address ==
                   MemoryMap::PLIC_ENABLE + (MemoryMap::UART_IRQ / 32) * 4) {
          data = plicEnableReg;
        } else if (address ==
                   MemoryMap::PLIC_PENDING + (MemoryMap::UART_IRQ / 32) * 4) {
          data = plicPendingReg;
        } else if (address == MemoryMap::PLIC_THRESHOLD) {
          data = plicThresholdReg;
        } else if (address == MemoryMap::PLIC_CLAIM) {
          data = ((plicPendingReg & plicEnableReg) & (1 << MemoryMap::UART_IRQ))
                     ? MemoryMap::UART_IRQ
                     : 0;
        } else if (address == MemoryMap::UART_BASE + 2) {
          data = 1;
        } else {
          handled = false;
        }
        if (handled) {
          files.output << hex_format(pc, 8) << ":lw     " << x_label[rd] << ","
                       << hex_format(imm & 0xFFF, 3) << "(" << x_label[rs1]
                       << ")      " << x_label[rd] << "=mem["
                       << hex_format(address, 8) << "]=" << hex_format(data, 8)
                       << std::endl;
        }
      }

      if (handled) {
        loadRd(data, rd, x);
      } else {
        triggerException(5, address, pc, mepc, mcause, mtvec, mtval, mstatus);
        files.output << ">exception:load_fault               cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      }
      break;
    }
    case 0b0100011: { // S-Type
      const int32_t simm = signedImmediate(immS);
      const uint32_t address = x[rs1] + simm;
      const uint32_t data = x[rs2];
      bool handled = true;

      if (address >= MemoryMap::OFFSET &&
          address < (MemoryMap::OFFSET + mem.size())) {
        dCache.write(address, data, funct3, mem);
        if (funct3 == 0b000) { // sb
          files.output << hex_format(pc, 8) << ":sb     " << x_label[rs2] << ","
                       << hex_format(immS, 3) << "(" << x_label[rs1]
                       << ")      mem[" << hex_format(address, 8)
                       << "]=" << hex_format(data & 0xFF, 2) << std::endl;
        } else if (funct3 == 0b001) { // sh
          files.output << hex_format(pc, 8) << ":sh     " << x_label[rs2] << ","
                       << hex_format(immS, 3) << "(" << x_label[rs1]
                       << ")      mem[" << hex_format(address, 8)
                       << "]=" << hex_format(data & 0xFFFF, 4) << std::endl;
        } else if (funct3 == 0b010) { // sw
          files.output << hex_format(pc, 8) << ":sw     " << x_label[rs2] << ","
                       << hex_format(immS, 3) << "(" << x_label[rs1]
                       << ")      mem[" << hex_format(address, 8)
                       << "]=" << hex_format(data, 8) << std::endl;
        } else {
          handled = false;
        }
      } else {
        if (address == MemoryMap::CLINT_MSIP) {
          clintMsip = data & 0x1;
        } else if (address == MemoryMap::CLINT_MTIMECMP) {
          mtimecmp = (mtimecmp & 0xFFFFFFFF00000000) | data;
        } else if (address == MemoryMap::CLINT_MTIMECMP + 4) {
          mtimecmp = (mtimecmp & 0x00000000FFFFFFFF) |
                     (static_cast<uint64_t>(data) << 32);
        } else if (address == MemoryMap::CLINT_MTIME) {
          mtime = (mtime & 0xFFFFFFFF00000000) | data;
        } else if (address == MemoryMap::CLINT_MTIME + 4) {
          mtime = (mtime & 0x00000000FFFFFFFF) |
                  (static_cast<uint64_t>(data) << 32);
        } else if (address == MemoryMap::UART_TX_REG) {
          files.terminalOutput.put(static_cast<char>(data));
          files.terminalOutput.flush();
          plicPendingReg |= (1 << MemoryMap::UART_IRQ);
        } else if (address ==
                   MemoryMap::PLIC_ENABLE + (MemoryMap::UART_IRQ / 32) * 4) {
          plicEnableReg = data;
        } else if (address == MemoryMap::PLIC_THRESHOLD) {
          plicThresholdReg = data;
        } else if (address == MemoryMap::PLIC_CLAIM) {
          if (data == MemoryMap::UART_IRQ)
            plicPendingReg &= ~(1 << MemoryMap::UART_IRQ);
        } else {
          handled = false;
        }
        if (handled)
          files.output << hex_format(pc, 8) << ":sw     " << x_label[rs2] << ","
                       << hex_format(immS, 3) << "(" << x_label[rs1]
                       << ")      mem[" << hex_format(address, 8)
                       << "]=" << hex_format(data, 8) << std::endl;
      }

      if (!handled) {
        triggerException(7, address, pc, mepc, mcause, mtvec, mtval, mstatus);
        files.output << ">exception:store_fault              cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      }
      break;
    }
    case 0b0110011: { // R-Type
      const uint32_t shift = x[rs2] & 0x1F;
      if (funct3 == 0b000 && funct7 == 0b0000000) { // add
        const uint32_t data = x[rs1] + x[rs2];
        files.output << hex_format(pc, 8) << ":add    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "+"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b000 && funct7 == 0b0100000) { // sub
        const uint32_t data = x[rs1] - x[rs2];
        files.output << hex_format(pc, 8) << ":sub    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "-"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b100 && funct7 == 0b0000000) { // xor
        const uint32_t data = x[rs1] ^ x[rs2];
        files.output << hex_format(pc, 8) << ":xor    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "^"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b110 && funct7 == 0b0000000) { // or
        const uint32_t data = x[rs1] | x[rs2];
        files.output << hex_format(pc, 8) << ":or     " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "|"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b111 && funct7 == 0b0000000) { // and
        const uint32_t data = x[rs1] & x[rs2];
        files.output << hex_format(pc, 8) << ":and    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "&"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b010 && funct7 == 0b0000000) { // slt
        const uint32_t data =
            (static_cast<int32_t>(x[rs1]) < static_cast<int32_t>(x[rs2])) ? 1
                                                                          : 0;
        files.output << hex_format(pc, 8) << ":slt    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "     "
                     << x_label[rd] << "=(" << hex_format(x[rs1], 8) << "<"
                     << hex_format(x[rs2], 8) << ")=" << std::dec << data
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b011 && funct7 == 0b0000000) { // sltu
        const uint32_t data = (x[rs1] < x[rs2]) ? 1 : 0;
        files.output << hex_format(pc, 8) << ":sltu   " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "     "
                     << x_label[rd] << "=(" << hex_format(x[rs1], 8) << "<"
                     << hex_format(x[rs2], 8) << ")=" << std::dec << data
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b001 && funct7 == 0b0000000) { // sll
        const uint32_t data = x[rs1] << shift;
        files.output << hex_format(pc, 8) << ":sll    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "<<"
                     << std::dec << shift << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000000) { // srl
        const uint32_t data = x[rs1] >> shift;
        files.output << hex_format(pc, 8) << ":srl    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << ">>"
                     << std::dec << shift << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0100000) { // sra
        const int32_t data = static_cast<int32_t>(x[rs1]) >> shift;
        files.output << hex_format(pc, 8) << ":sra    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << ">>>"
                     << std::dec << shift << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b000 && funct7 == 0b0000001) { // mul
        const int64_t product =
            static_cast<int64_t>(static_cast<int32_t>(x[rs1])) *
            static_cast<int64_t>(static_cast<int32_t>(x[rs2]));
        files.output << hex_format(pc, 8) << ":mul    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "*"
                     << hex_format(x[rs2], 8) << "="
                     << hex_format(static_cast<uint32_t>(product), 8)
                     << std::endl;
        loadRd(static_cast<uint32_t>(product), rd, x);
      } else if (funct3 == 0b001 && funct7 == 0b0000001) { // mulh
        const int64_t product =
            static_cast<int64_t>(static_cast<int32_t>(x[rs1])) *
            static_cast<int64_t>(static_cast<int32_t>(x[rs2]));
        files.output << hex_format(pc, 8) << ":mulh   " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "*"
                     << hex_format(x[rs2], 8) << "="
                     << hex_format(static_cast<uint32_t>(product >> 32), 8)
                     << std::endl;
        loadRd(static_cast<uint32_t>(product >> 32), rd, x);
      } else if (funct3 == 0b010 && funct7 == 0b0000001) { // mulhsu
        const int64_t product =
            static_cast<int64_t>(static_cast<int32_t>(x[rs1])) *
            static_cast<uint64_t>(x[rs2]);
        files.output << hex_format(pc, 8) << ":mulhsu " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "*"
                     << hex_format(x[rs2], 8) << "="
                     << hex_format(static_cast<uint32_t>(product >> 32), 8)
                     << std::endl;
        loadRd(static_cast<uint32_t>(product >> 32), rd, x);
      } else if (funct3 == 0b011 && funct7 == 0b0000001) { // mulhu
        const uint64_t product =
            static_cast<uint64_t>(x[rs1]) * static_cast<uint64_t>(x[rs2]);
        files.output << hex_format(pc, 8) << ":mulhu  " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "*"
                     << hex_format(x[rs2], 8) << "="
                     << hex_format(static_cast<uint32_t>(product >> 32), 8)
                     << std::endl;
        loadRd(static_cast<uint32_t>(product >> 32), rd, x);
      } else if (funct3 == 0b100 && funct7 == 0b0000001) { // div
        int32_t dividend = x[rs1];
        int32_t divisor = x[rs2];
        int32_t data;
        if (divisor == 0)
          data = -1;
        else if (dividend == INT32_MIN && divisor == -1)
          data = INT32_MIN;
        else
          data = dividend / divisor;
        files.output << hex_format(pc, 8) << ":div    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "/"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b101 && funct7 == 0b0000001) { // divu
        uint32_t dividend = x[rs1];
        uint32_t divisor = x[rs2];
        uint32_t data = (divisor == 0) ? UINT32_MAX : dividend / divisor;
        files.output << hex_format(pc, 8) << ":divu   " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "/"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b110 && funct7 == 0b0000001) { // rem
        int32_t dividend = x[rs1];
        int32_t divisor = x[rs2];
        int32_t data;
        if (divisor == 0)
          data = dividend;
        else if (dividend == INT32_MIN && divisor == -1)
          data = 0;
        else
          data = dividend % divisor;
        files.output << hex_format(pc, 8) << ":rem    " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "%"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      } else if (funct3 == 0b111 && funct7 == 0b0000001) { // remu
        uint32_t dividend = x[rs1];
        uint32_t divisor = x[rs2];
        uint32_t data = (divisor == 0) ? dividend : dividend % divisor;
        files.output << hex_format(pc, 8) << ":remu   " << x_label[rd] << ","
                     << x_label[rs1] << "," << x_label[rs2] << "       "
                     << x_label[rd] << "=" << hex_format(x[rs1], 8) << "%"
                     << hex_format(x[rs2], 8) << "=" << hex_format(data, 8)
                     << std::endl;
        loadRd(data, rd, x);
      }
      break;
    }
    case 0b1100011: { // B-Type
      uint32_t nextPc = pc + 4;
      bool taken = false;
      if (funct3 == 0b000) { // beq
        if (x[rs1] == x[rs2])
          taken = true;
      } else if (funct3 == 0b001) { // bne
        if (x[rs1] != x[rs2])
          taken = true;
      } else if (funct3 == 0b100) { // blt
        if (static_cast<int32_t>(x[rs1]) < static_cast<int32_t>(x[rs2]))
          taken = true;
      } else if (funct3 == 0b101) { // bge
        if (static_cast<int32_t>(x[rs1]) >= static_cast<int32_t>(x[rs2]))
          taken = true;
      } else if (funct3 == 0b110) { // bltu
        if (x[rs1] < x[rs2])
          taken = true;
      } else if (funct3 == 0b111) { // bgeu
        if (x[rs1] >= x[rs2])
          taken = true;
      }

      if (taken)
        nextPc = pc + branchImm;

      files.output
          << hex_format(pc, 8) << ":b"
          << (funct3 == 0
                  ? "eq"
                  : (funct3 == 1
                         ? "ne"
                         : (funct3 == 4
                                ? "lt"
                                : (funct3 == 5
                                       ? "ge"
                                       : (funct3 == 6 ? "ltu" : "geu")))))
          << "    " << x_label[rs1] << "," << x_label[rs2] << ","
          << hex_format(branchImm, 3) << "        (" << hex_format(x[rs1], 8)
          << (funct3 == 0
                  ? "=="
                  : (funct3 == 1
                         ? "!="
                         : (funct3 == 4
                                ? "<"
                                : (funct3 == 5 ? ">="
                                               : (funct3 == 6 ? "<" : ">=")))))
          << hex_format(x[rs2], 8) << ")=" << taken
          << "->pc=" << hex_format(nextPc, 8) << std::endl;

      if (taken)
        pc = nextPc - 4;
      break;
    }
    case 0b1101111: { // JAL
      const uint32_t data = pc + 4;
      const uint32_t address = pc + jalOffset;
      files.output << hex_format(pc, 8) << ":jal    " << x_label[rd] << ","
                   << hex_format((jalOffset / 2), 5)
                   << "         pc=" << hex_format(address, 8) << ","
                   << x_label[rd] << "=" << hex_format(data, 8) << std::endl;
      loadRd(data, rd, x);
      pc = address - 4;
      break;
    }
    case 0b1100111: { // JALR
      if (funct3 == 0b000) {
        const int32_t simm = signedImmediate(imm);
        const uint32_t data = pc + 4;
        uint32_t address = (x[rs1] + simm);
        files.output << hex_format(pc, 8) << ":jalr   " << x_label[rd] << ","
                     << x_label[rs1] << "," << hex_format(imm & 0xFFF, 3)
                     << "    pc=" << hex_format(x[rs1], 8) << "+"
                     << hex_format(simm, 8) << "," << x_label[rd] << "="
                     << hex_format(data, 8) << std::endl;
        loadRd(data, rd, x);
        pc = (address & ~1) - 4;
      }
      break;
    }
    case 0b1110011: { // SYSTEM
      const uint16_t csrAddress = imm;
      const uint8_t uimm_csr = rs1;
      if (funct3 == 0b000 && csrAddress == 0) { // ecall
        files.output << hex_format(pc, 8) << ":ecall" << std::endl;
        triggerException(11, pc, pc, mepc, mcause, mtvec, mtval, mstatus);
        files.output << ">exception:environment_call        cause="
                     << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                     << ",tval=" << hex_format(mtval, 8) << std::endl;
        continue;
      } else if (funct3 == 0b000 && csrAddress == 0x302) { // mret
        files.output << hex_format(pc, 8) << ":mret                         pc="
                     << hex_format(mepc, 8) << std::endl;
        uint32_t mpie = (mstatus >> 7) & 1;
        mstatus &= ~(0b11 << 11);
        mstatus |= (0b11 << 11);
        mstatus &= ~(1 << 3);
        mstatus |= (mpie << 3);
        mstatus |= (1 << 7);
        pc = mepc;
        continue;
      } else if (funct3 == 0b000 && csrAddress == 1) { // ebreak
        files.output << hex_format(pc, 8) << ":ebreak" << std::endl;
        run = false;
      } else {
        uint32_t oldCsrValue =
            readCsr(csrAddress, mepc, mcause, mtvec, mtval, mstatus, mie, mip);
        uint32_t newCsrValue;

        switch (funct3) {
        case 0b001: // CSRRW
          newCsrValue = x[rs1];
          files.output << hex_format(pc, 8) << ":csrrw  " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << x_label[rs1]
                       << "       " << x_label[rd] << "="
                       << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress) << "=" << x_label[rs1] << "="
                       << hex_format(newCsrValue, 8) << std::endl;
          writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval, mstatus,
                   mie, mip);
          loadRd(oldCsrValue, rd, x);
          break;
        case 0b010: // CSRRS
          newCsrValue = oldCsrValue | x[rs1];
          files.output << hex_format(pc, 8) << ":csrrs  " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << x_label[rs1]
                       << "       " << x_label[rd] << "="
                       << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress) << "|=" << x_label[rs1] << "="
                       << hex_format(oldCsrValue, 8) << "|"
                       << hex_format(x[rs1], 8) << "="
                       << hex_format(newCsrValue, 8) << std::endl;
          loadRd(oldCsrValue, rd, x);
          if (rs1 != 0) {
            writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval,
                     mstatus, mie, mip);
          }
          break;
        case 0b011: // CSRRC
          newCsrValue = oldCsrValue & ~x[rs1];
          files.output << hex_format(pc, 8) << ":csrrc  " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << x_label[rs1]
                       << "       " << x_label[rd] << "="
                       << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress) << "&~=" << x_label[rs1] << "="
                       << hex_format(oldCsrValue, 8) << "&~"
                       << hex_format(x[rs1], 8) << "="
                       << hex_format(newCsrValue, 8) << std::endl;
          loadRd(oldCsrValue, rd, x);
          if (rs1 != 0) {
            writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval,
                     mstatus, mie, mip);
          }
          break;
        case 0b101: // CSRRWI
          newCsrValue = uimm_csr;
          files.output << hex_format(pc, 8) << ":csrrwi " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << std::dec
                       << static_cast<unsigned int>(uimm_csr) << "        "
                       << x_label[rd] << "=" << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress)
                       << "=u5=" << hex_format(newCsrValue, 8) << std::endl;
          writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval, mstatus,
                   mie, mip);
          loadRd(oldCsrValue, rd, x);
          break;
        case 0b110: // CSRRSI
          newCsrValue = oldCsrValue | uimm_csr;
          files.output << hex_format(pc, 8) << ":csrrsi " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << std::dec
                       << static_cast<unsigned int>(uimm_csr) << "        "
                       << x_label[rd] << "=" << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress)
                       << "|=u5=" << hex_format(oldCsrValue, 8) << "|"
                       << hex_format(uimm_csr, 8) << "="
                       << hex_format(newCsrValue, 8) << std::endl;
          loadRd(oldCsrValue, rd, x);
          if (uimm_csr != 0) {
            writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval,
                     mstatus, mie, mip);
          }
          break;
        case 0b111: // CSRRCI
          newCsrValue = oldCsrValue & ~uimm_csr;
          files.output << hex_format(pc, 8) << ":csrrci " << x_label[rd] << ","
                       << getCsrName(csrAddress) << "," << std::dec
                       << static_cast<unsigned int>(uimm_csr) << "        "
                       << x_label[rd] << "=" << getCsrName(csrAddress) << "="
                       << hex_format(oldCsrValue, 8) << ","
                       << getCsrName(csrAddress)
                       << "&~=u5=" << hex_format(oldCsrValue, 8) << "&~"
                       << hex_format(uimm_csr, 8) << "="
                       << hex_format(newCsrValue, 8) << std::endl;
          loadRd(oldCsrValue, rd, x);
          if (uimm_csr != 0) {
            writeCsr(csrAddress, newCsrValue, mepc, mcause, mtvec, mtval,
                     mstatus, mie, mip);
          }
          break;
        default:
          triggerException(2, instruction, pc, mepc, mcause, mtvec, mtval,
                           mstatus);
          files.output << ">exception:illegal_instruction   cause="
                       << hex_format(mcause, 8)
                       << ",epc=" << hex_format(mepc, 8)
                       << ",tval=" << hex_format(instruction, 8) << std::endl;
          continue;
        }
      }
      break;
    }
    default:
      triggerException(2, instruction, pc, mepc, mcause, mtvec, mtval, mstatus);
      files.output << ">exception:illegal_instruction   cause="
                   << hex_format(mcause, 8) << ",epc=" << hex_format(mepc, 8)
                   << ",tval=" << hex_format(instruction, 8) << std::endl;
      continue;
    }

    pc += 4;
    mtime++;
  }

  dCache.printStats();
  iCache.printStats();

  return 0;
}
