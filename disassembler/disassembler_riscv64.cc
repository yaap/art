/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "disassembler_riscv64.h"

#include "android-base/logging.h"
#include "android-base/stringprintf.h"

#include "base/bit_utils.h"
#include "base/casts.h"

using android::base::StringPrintf;

namespace art {
namespace riscv64 {

class DisassemblerRiscv64::Printer {
 public:
  Printer(DisassemblerRiscv64* disassembler, std::ostream& os)
      : disassembler_(disassembler), os_(os) {}

  void Dump32(const uint8_t* insn);
  void Dump16(const uint8_t* insn);
  void Dump2Byte(const uint8_t* data);
  void DumpByte(const uint8_t* data);

 private:
  // This enumeration should mirror the declarations in runtime/arch/riscv64/registers_riscv64.h.
  // We do not include that file to avoid a dependency on libart.
  enum {
    Zero = 0,
    RA = 1,
    FP  = 8,
    TR  = 9,
  };

  static const char* XRegName(uint32_t regno);
  static const char* FRegName(uint32_t regno);

  static int32_t Decode32Imm12(uint32_t insn32) {
    uint32_t sign = (insn32 >> 31);
    uint32_t imm12 = (insn32 >> 20);
    return static_cast<int32_t>(imm12) - static_cast<int32_t>(sign << 12);  // Sign-extend.
  }

  static int32_t Decode32StoreOffset(uint32_t insn32) {
    uint32_t bit11 = insn32 >> 31;
    uint32_t bits5_11 = insn32 >> 25;
    uint32_t bits0_4 = (insn32 >> 7) & 0x1fu;
    uint32_t imm = (bits5_11 << 5) + bits0_4;
    return static_cast<int32_t>(imm) - static_cast<int32_t>(bit11 << 12);  // Sign-extend.
  }

  static uint32_t GetRd(uint32_t insn32) { return (insn32 >> 7) & 0x1fu; }
  static uint32_t GetRs1(uint32_t insn32) { return (insn32 >> 15) & 0x1fu; }
  static uint32_t GetRs2(uint32_t insn32) { return (insn32 >> 20) & 0x1fu; }

  void PrintBranchOffset(int32_t offset);
  void PrintLoadStoreAddress(uint32_t rs1, int32_t offset);

  void Print32Lui(uint32_t insn32);
  void Print32Auipc(const uint8_t* insn, uint32_t insn32);
  void Print32Jal(const uint8_t* insn, uint32_t insn32);
  void Print32Jalr(const uint8_t* insn, uint32_t insn32);
  void Print32BCond(const uint8_t* insn, uint32_t insn32);
  void Print32Load(uint32_t insn32);
  void Print32Store(uint32_t insn32);
  void Print32FLoad(uint32_t insn32);
  void Print32FStore(uint32_t insn32);
  void Print32BinOpImm(uint32_t insn32);
  void Print32BinOp(uint32_t insn32);

  DisassemblerRiscv64* const disassembler_;
  std::ostream& os_;
};

const char* DisassemblerRiscv64::Printer::XRegName(uint32_t regno) {
  static const char* const kXRegisterNames[] = {
      "zero",
      "ra",
      "sp",
      "gp",
      "tp",
      "t0",
      "t1",
      "t2",
      "fp",  // s0/fp
      "tr",  // s1/tr - ART thread register
      "a0",
      "a1",
      "a2",
      "a3",
      "a4",
      "a5",
      "a6",
      "a7",
      "s2",
      "s3",
      "s4",
      "s5",
      "s6",
      "s7",
      "s8",
      "s9",
      "s10",
      "s11",
      "t3",
      "t4",
      "t5",
      "t6",
  };
  static_assert(std::size(kXRegisterNames) == 32);
  DCHECK_LT(regno, 32u);
  return kXRegisterNames[regno];
}

const char* DisassemblerRiscv64::Printer::FRegName(uint32_t regno) {
  static const char* const kFRegisterNames[] = {
      "ft0",
      "ft1",
      "ft2",
      "ft3",
      "ft4",
      "ft5",
      "ft6",
      "ft7",
      "fs0",
      "fs1",
      "fa0",
      "fa1",
      "fa2",
      "fa3",
      "fa4",
      "fa5",
      "fa6",
      "fa7",
      "fs2",
      "fs3",
      "fs4",
      "fs5",
      "fs6",
      "fs7",
      "fs8",
      "fs9",
      "fs10",
      "fs11",
      "ft8",
      "ft9",
      "ft10",
      "ft11",
  };
  static_assert(std::size(kFRegisterNames) == 32);
  DCHECK_LT(regno, 32u);
  return kFRegisterNames[regno];
}

void DisassemblerRiscv64::Printer::PrintBranchOffset(int32_t offset) {
  os_ << (offset >= 0 ? "+" : "") << offset;
}

void DisassemblerRiscv64::Printer::PrintLoadStoreAddress(uint32_t rs1, int32_t offset) {
  if (offset != 0) {
    os_ << StringPrintf("%d", offset);
  }
  os_ << "(" << XRegName(rs1) << ")";

  if (rs1 == TR && offset >= 0) {
    // Add entrypoint name.
    os_ << " ; ";
    disassembler_->GetDisassemblerOptions()->thread_offset_name_function_(
        os_, dchecked_integral_cast<uint32_t>(offset));
  }
}

void DisassemblerRiscv64::Printer::Print32Lui(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x37u);
  // TODO(riscv64): Should we also print the actual sign-extend value?
  os_ << StringPrintf("lui %s, %u", XRegName(GetRd(insn32)), insn32 >> 12);
}

void DisassemblerRiscv64::Printer::Print32Auipc([[maybe_unused]] const uint8_t* insn,
                                                uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x17u);
  // TODO(riscv64): Should we also print the calculated address?
  os_ << StringPrintf("auipc %s, %u", XRegName(GetRd(insn32)), insn32 >> 12);
}

void DisassemblerRiscv64::Printer::Print32Jal(const uint8_t* insn, uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x6fu);
  // Print an alias if available.
  uint32_t rd = GetRd(insn32);
  os_ << (rd == Zero ? "j " : "jal ");
  if (rd != Zero && rd != RA) {
    os_ << XRegName(rd) << ", ";
  }
  uint32_t bit20 = (insn32 >> 31);
  uint32_t bits1_10 = (insn32 >> 21) & 0x3ffu;
  uint32_t bit11 = (insn32 >> 20) & 1u;
  uint32_t bits12_19 = (insn32 >> 12) & 0xffu;
  uint32_t imm = (bits1_10 << 1) + (bit11 << 11) + (bits12_19 << 12) + (bit20 << 20);
  int32_t offset = static_cast<int32_t>(imm) - static_cast<int32_t>(bit20 << 21);  // Sign-extend.
  PrintBranchOffset(offset);
  os_ << " ; " << disassembler_->FormatInstructionPointer(insn + offset);

  // TODO(riscv64): When we implement shared thunks to reduce AOT slow-path code size,
  // check if this JAL lands at an entrypoint load from TR and, if so, print its name.
}

void DisassemblerRiscv64::Printer::Print32Jalr([[maybe_unused]] const uint8_t* insn,
                                               uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x67u);
  DCHECK_EQ((insn32 >> 12) & 7u, 0u);
  uint32_t rd = GetRd(insn32);
  uint32_t rs1 = GetRs1(insn32);
  int32_t imm12 = Decode32Imm12(insn32);
  // Print shorter macro instruction notation if available.
  if (rd == Zero && rs1 == RA && imm12 == 0) {
    os_ << "ret";
  } else if (rd == Zero && imm12 == 0) {
    os_ << "jr " << XRegName(rs1);
  } else if (rd == RA && imm12 == 0) {
    os_ << "jalr " << XRegName(rs1);
  } else {
    // TODO(riscv64): Should we also print the calculated address if the preceding
    // instruction is AUIPC? (We would need to record the previous instruction.)
    os_ << "jalr " << XRegName(rd) << ", ";
    // Use the same format as llvm-objdump: "rs1" if `imm12` is zero, otherwise "imm12(rs1)".
    if (imm12 == 0) {
      os_ << XRegName(rs1);
    } else {
      os_ << imm12 << "(" << XRegName(rs1) << ")";
    }
  }
}

void DisassemblerRiscv64::Printer::Print32BCond(const uint8_t* insn, uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x63u);
  static const char* const kOpcodes[] = {
      "beq", "bne", nullptr, nullptr, "blt", "bge", "bltu", "bgeu"
  };
  uint32_t funct3 = (insn32 >> 12) & 7u;
  const char* opcode = kOpcodes[funct3];
  if (opcode == nullptr) {
    os_ << "<unknown32>";
    return;
  }

  // Print shorter macro instruction notation if available.
  uint32_t rs1 = GetRs1(insn32);
  uint32_t rs2 = GetRs2(insn32);
  if (rs2 == Zero) {
    os_ << opcode << "z " << XRegName(rs1);
  } else if (rs1 == Zero && (funct3 == 4u || funct3 == 5u)) {
    // blt zero, rs2, offset ... bgtz rs2, offset
    // bge zero, rs2, offset ... blez rs2, offset
    os_ << (funct3 == 4u ? "bgtz " : "blez ") << XRegName(rs2);
  } else {
    os_ << opcode << " " << XRegName(rs1) << ", " << XRegName(rs2);
  }
  os_ << ", ";

  uint32_t bit12 = insn32 >> 31;
  uint32_t bits5_10 = (insn32 >> 25) & 0x3fu;
  uint32_t bits1_4 = (insn32 >> 8) & 0xfu;
  uint32_t bit11 = (insn32 >> 7) & 1u;
  uint32_t imm = (bit12 << 12) + (bit11 << 11) + (bits5_10 << 5) + (bits1_4 << 1);
  int32_t offset = static_cast<int32_t>(imm) - static_cast<int32_t>(bit12 << 13);  // Sign-extend.
  PrintBranchOffset(offset);
  os_ << " ; " << disassembler_->FormatInstructionPointer(insn + offset);
}

void DisassemblerRiscv64::Printer::Print32Load(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x03u);
  static const char* const kOpcodes[] = {
      "lb", "lh", "lw", "ld", "lbu", "lhu", "lwu", nullptr
  };
  uint32_t funct3 = (insn32 >> 12) & 7u;
  const char* opcode = kOpcodes[funct3];
  if (opcode == nullptr) {
    os_ << "<unknown32>";
    return;
  }

  os_ << opcode << " " << XRegName(GetRd(insn32)) << ", ";
  PrintLoadStoreAddress(GetRs1(insn32), Decode32Imm12(insn32));

  // TODO(riscv64): If previous instruction is AUIPC for current `rs1` and we load
  // from the range specified by assembler options, print the loaded literal.
}

void DisassemblerRiscv64::Printer::Print32Store(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x23u);
  static const char* const kOpcodes[] = {
      "sb", "sh", "sw", "sd", nullptr, nullptr, nullptr, nullptr
  };
  uint32_t funct3 = (insn32 >> 12) & 7u;
  const char* opcode = kOpcodes[funct3];
  if (opcode == nullptr) {
    os_ << "<unknown32>";
    return;
  }

  os_ << opcode << " " << XRegName(GetRs2(insn32)) << ", ";
  PrintLoadStoreAddress(GetRs1(insn32), Decode32StoreOffset(insn32));
}

void DisassemblerRiscv64::Printer::Print32FLoad(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x07u);
  static const char* const kOpcodes[] = {
      nullptr, nullptr, "flw", "fld", nullptr, nullptr, nullptr, nullptr
  };
  uint32_t funct3 = (insn32 >> 12) & 7u;
  const char* opcode = kOpcodes[funct3];
  if (opcode == nullptr) {
    os_ << "<unknown32>";
    return;
  }

  os_ << opcode << " " << FRegName(GetRd(insn32)) << ", ";
  PrintLoadStoreAddress(GetRs1(insn32), Decode32Imm12(insn32));

  // TODO(riscv64): If previous instruction is AUIPC for current `rs1` and we load
  // from the range specified by assembler options, print the loaded literal.
}

void DisassemblerRiscv64::Printer::Print32FStore(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x7fu, 0x27u);
  static const char* const kOpcodes[] = {
      nullptr, nullptr, "fsw", "fsd", nullptr, nullptr, nullptr, nullptr
  };
  uint32_t funct3 = (insn32 >> 12) & 7u;
  const char* opcode = kOpcodes[funct3];
  if (opcode == nullptr) {
    os_ << "<unknown32>";
    return;
  }

  os_ << opcode << " " << FRegName(GetRs2(insn32)) << ", ";
  PrintLoadStoreAddress(GetRs1(insn32), Decode32StoreOffset(insn32));
}

void DisassemblerRiscv64::Printer::Print32BinOpImm(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x77u, 0x13u);  // Note: Bit 0x8 selects narrow binop.
  bool narrow = (insn32 & 0x8u) != 0u;
  uint32_t funct3 = (insn32 >> 12) & 7u;
  uint32_t rd = GetRd(insn32);
  uint32_t rs1 = GetRs1(insn32);
  int32_t imm = Decode32Imm12(insn32);

  // Print shorter macro instruction notation if available.
  if (funct3 == /*ADDI*/ 0u && imm == 0u) {
    if (narrow) {
      os_ << "sextw " << XRegName(rd) << ", " << XRegName(rs1);
    } else if (rd == Zero && rs1 == Zero) {
      os_ << "nop";  // Only canonical nop. Non-Zero `rd == rs1` nops are printed as "mv".
    } else {
      os_ << "mv " << XRegName(rd) << ", " << XRegName(rs1);
    }
  } else if (!narrow && funct3 == /*XORI*/ 4u && imm == -1) {
    os_ << "not " << XRegName(rd) << ", " << XRegName(rs1);
  } else if (!narrow && funct3 == /*ANDI*/ 7u && imm == 0xff) {
    os_ << "zextb " << XRegName(rd) << ", " << XRegName(rs1);
  } else if (!narrow && funct3 == /*SLTIU*/ 3u && imm == 1) {
    os_ << "seqz " << XRegName(rd) << ", " << XRegName(rs1);
  } else {
    bool bad_high_bits = false;
    if (funct3 == /*SLLI*/ 1u || funct3 == /*SRLI/SRAI*/ 5u) {
      uint32_t high_bits = insn32 & (narrow ? 0xfe000000u : 0xfc000000u);
      if (high_bits == 0x40000000u && funct3 == /*SRAI*/ 5u) {
        os_ << "srai";
      } else {
        os_ << ((funct3 == /*SRLI*/ 5u) ? "srli" : "slli");
        imm &= (narrow ? 0x1fu : 0x3fu);
        bad_high_bits = (high_bits != 0u);
      }
    } else if (!narrow || funct3 == /*ADDI*/ 0u) {
      static const char* const kOpcodes[] = {
          "addi", nullptr, "slti", "sltiu", "xori", nullptr, "ori", "andi"
      };
      DCHECK(kOpcodes[funct3] != nullptr);
      os_ << kOpcodes[funct3];
    } else {
      os_ << "<unknown32>";  // There is no SLTIW/SLTIUW/XORIW/ORIW/ANDIW.
      return;
    }
    os_ << (narrow ? "w " : " ") << XRegName(rd) << ", " << XRegName(rs1) << ", " << imm;
    if (bad_high_bits) {
      os_ << " (invalid high bits)";
    }
  }
}

void DisassemblerRiscv64::Printer::Print32BinOp(uint32_t insn32) {
  DCHECK_EQ(insn32 & 0x77u, 0x33u);  // Note: Bit 0x8 selects narrow binop.
  bool narrow = (insn32 & 0x8u) != 0u;
  uint32_t funct3 = (insn32 >> 12) & 7u;
  uint32_t rd = GetRd(insn32);
  uint32_t rs1 = GetRs1(insn32);
  uint32_t rs2 = GetRs2(insn32);
  uint32_t high_bits = insn32 & 0xfe000000u;

  // Print shorter macro instruction notation if available.
  if (high_bits == 0x40000000u && funct3 == /*SUB*/ 0u && rs1 == Zero) {
    os_ << (narrow ? "negw " : "neg ") << XRegName(rd) << ", " << XRegName(rs2);
  } else if (!narrow && funct3 == /*SLT*/ 2u && rs2 == Zero) {
    os_ << "sltz " << XRegName(rd) << ", " << XRegName(rs1);
  } else if (!narrow && funct3 == /*SLT*/ 2u && rs1 == Zero) {
    os_ << "sgtz " << XRegName(rd) << ", " << XRegName(rs2);
  } else if (!narrow && funct3 == /*SLTU*/ 3u && rs1 == Zero) {
    os_ << "snez " << XRegName(rd) << ", " << XRegName(rs2);
  } else {
    bool bad_high_bits = false;
    if (high_bits == 0x40000000u && (funct3 == /*SUB*/ 0u || funct3 == /*SRA*/ 5u)) {
      os_ << ((funct3 == /*SUB*/ 0u) ? "sub" : "sra");
    } else if (!narrow || (funct3 == /*ADD*/ 0u || funct3 == /*SLL*/ 1u || funct3 == /*SRL*/ 5u)) {
      static const char* const kOpcodes[] = {
          "add", "sll", "slt", "sltu", "xor", "srl", "or", "and"
      };
      os_ << kOpcodes[funct3];
      bad_high_bits = (high_bits != 0u);
    } else {
      os_ << "<unknown32>";  // There is no SLTW/SLTUW/XORW/ORW/ANDW.
      return;
    }
    os_ << (narrow ? "w " : " ") << XRegName(rd) << ", " << XRegName(rs1) << ", " << XRegName(rs2);
    if (bad_high_bits) {
      os_ << " (invalid high bits)";
    }
  }
}

void DisassemblerRiscv64::Printer::Dump32(const uint8_t* insn) {
  uint32_t insn32 = static_cast<uint32_t>(insn[0]) +
                    (static_cast<uint32_t>(insn[1]) << 8) +
                    (static_cast<uint32_t>(insn[2]) << 16) +
                    (static_cast<uint32_t>(insn[3]) << 24);
  CHECK_EQ(insn32 & 3u, 3u);
  os_ << disassembler_->FormatInstructionPointer(insn) << StringPrintf(": %08x\t", insn32);
  switch (insn32 & 0x7fu) {
    case 0x37u:
      Print32Lui(insn32);
      break;
    case 0x17u:
      Print32Auipc(insn, insn32);
      break;
    case 0x6fu:
      Print32Jal(insn, insn32);
      break;
    case 0x67u:
      switch ((insn32 >> 12) & 7u) {  // funct3
        case 0:
          Print32Jalr(insn, insn32);
          break;
        default:
          os_ << "<unknown32>";
          break;
      }
      break;
    case 0x63u:
      Print32BCond(insn, insn32);
      break;
    case 0x03u:
      Print32Load(insn32);
      break;
    case 0x23u:
      Print32Store(insn32);
      break;
    case 0x07u:
      Print32FLoad(insn32);
      break;
    case 0x27u:
      Print32FStore(insn32);
      break;
    case 0x13u:
    case 0x1bu:
      Print32BinOpImm(insn32);
      break;
    case 0x33u:
    case 0x3bu:
      Print32BinOp(insn32);
      break;
    default:
      // TODO(riscv64): Disassemble more instructions.
      os_ << "<unknown32>";
      break;
  }
  os_ << "\n";
}

void DisassemblerRiscv64::Printer::Dump16(const uint8_t* insn) {
  uint32_t insn16 = static_cast<uint32_t>(insn[0]) + (static_cast<uint32_t>(insn[1]) << 8);
  CHECK_NE(insn16 & 3u, 3u);
  // TODO(riscv64): Disassemble instructions from the "C" extension.
  os_ << disassembler_->FormatInstructionPointer(insn)
      << StringPrintf(": %04x    \t<unknown16>\n", insn16);
}

void DisassemblerRiscv64::Printer::Dump2Byte(const uint8_t* data) {
  uint32_t value = data[0] + (data[1] << 8);
  os_ << disassembler_->FormatInstructionPointer(data)
      << StringPrintf(": %04x    \t.2byte %u\n", value, value);
}

void DisassemblerRiscv64::Printer::DumpByte(const uint8_t* data) {
  uint32_t value = *data;
  os_ << disassembler_->FormatInstructionPointer(data)
      << StringPrintf(": %02x      \t.byte %u\n", value, value);
}

size_t DisassemblerRiscv64::Dump(std::ostream& os, const uint8_t* begin) {
  if (begin < GetDisassemblerOptions()->base_address_ ||
      begin >= GetDisassemblerOptions()->end_address_) {
    return 0u;  // Outside the range.
  }
  Printer printer(this, os);
  if (!IsAligned<2u>(begin) || GetDisassemblerOptions()->end_address_ - begin == 1) {
    printer.DumpByte(begin);
    return 1u;
  }
  if ((*begin & 3u) == 3u) {
    if (GetDisassemblerOptions()->end_address_ - begin >= 4) {
      printer.Dump32(begin);
      return 4u;
    } else {
      printer.Dump2Byte(begin);
      return 2u;
    }
  } else {
    printer.Dump16(begin);
    return 2u;
  }
}

void DisassemblerRiscv64::Dump(std::ostream& os, const uint8_t* begin, const uint8_t* end) {
  Printer printer(this, os);
  const uint8_t* cur = begin;
  if (cur < end && !IsAligned<2u>(cur)) {
    // Unaligned, dump as a `.byte` to get to an aligned address.
    printer.DumpByte(cur);
    cur += 1;
  }
  if (cur >= end) {
    return;
  }
  while (end - cur >= 4) {
    if ((*cur & 3u) == 3u) {
      printer.Dump32(cur);
      cur += 4;
    } else {
      printer.Dump16(cur);
      cur += 2;
    }
  }
  if (end - cur >= 2) {
    if ((*cur & 3u) == 3u) {
      // Not enough data for a 32-bit instruction. Dump as `.2byte`.
      printer.Dump2Byte(cur);
    } else {
      printer.Dump16(cur);
    }
    cur += 2;
  }
  if (end != cur) {
    CHECK_EQ(end - cur, 1);
    printer.DumpByte(cur);
  }
}

}  // namespace riscv64
}  // namespace art