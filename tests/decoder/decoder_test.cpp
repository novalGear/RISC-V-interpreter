#include "decoder/decode_table.hpp"
#include "decoder/decoder.hpp"
#include "disassembler/disassembler.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>
#include <string_view>
#include <utility>

namespace {

using riscv::decoder::Decode;
using riscv::decoder::DecodedInstruction;
using riscv::disassembler::RecordKind;

bool Expect (bool Condition, std::string_view Description)
{
  if (!Condition) {
    std::cerr << "FAILED: " << Description << '\n';
  }

  return Condition;
}

bool ExpectDecodeError (
  std::uint32_t Word,
  riscv::decoder::DecodeError Error,
  std::string_view Description
  )
{
  const auto Result = Decode (Word);

  return Expect (!Result.has_value (), Description) &&
         Expect (Result.error () == Error, "decode error kind");
}

const riscv::decoder::DecodedOperand *FindOperand (
  const DecodedInstruction &Instruction,
  std::string_view Name
  )
{
  for (const auto &Operand : Instruction.Operands ()) {
    if (Operand.name == Name) {
      return &Operand;
    }
  }

  return nullptr;
}

bool ExpectOperand (
  const DecodedInstruction &Instruction,
  std::string_view Name,
  std::uint32_t Value
  )
{
  const auto *Operand = FindOperand (Instruction, Name);
  return Expect (Operand != nullptr, "operand should exist") &&
         Expect (Operand->value == Value, Name);
}

bool ExpectDecode (
  std::uint32_t Word,
  std::string_view Name,
  std::span<const std::pair<std::string_view, std::uint32_t>> Operands
  )
{
  const auto Instruction = Decode (Word);
  if (!Expect (Instruction.has_value (), "known word should decode")) {
    return false;
  }

  bool Passed = Expect (
    riscv::decoder::Describe (Instruction->Id ()).name == Name,
    "instruction name"
    );
  Passed = Expect (Instruction->Operands ().size () == Operands.size (), "operand count") && Passed;

  for (const auto &[OperandName, Value] : Operands) {
    Passed = ExpectOperand (*Instruction, OperandName, Value) && Passed;
  }

  return Passed;
}

bool TestRepresentativeInstructions ()
{
  const std::array AddOperands{
    std::pair<std::string_view, std::uint32_t>{"rd", 1},
    std::pair<std::string_view, std::uint32_t>{"rs1", 2},
    std::pair<std::string_view, std::uint32_t>{"rs2", 3},
  };
  const std::array AddiOperands{
    std::pair<std::string_view, std::uint32_t>{"rd", 1},
    std::pair<std::string_view, std::uint32_t>{"rs1", 2},
    std::pair<std::string_view, std::uint32_t>{"imm", 0xffffffffU},
  };
  const std::array StoreOperands{
    std::pair<std::string_view, std::uint32_t>{"rs1", 2},
    std::pair<std::string_view, std::uint32_t>{"rs2", 3},
    std::pair<std::string_view, std::uint32_t>{"imm", 20},
  };
  const std::array BranchOperands{
    std::pair<std::string_view, std::uint32_t>{"rs1", 1},
    std::pair<std::string_view, std::uint32_t>{"rs2", 2},
    std::pair<std::string_view, std::uint32_t>{"imm", 0xfffffff8U},
  };
  const std::array NegativeStoreOperands{
    std::pair<std::string_view, std::uint32_t>{"rs1", 2},
    std::pair<std::string_view, std::uint32_t>{"rs2", 3},
    std::pair<std::string_view, std::uint32_t>{"imm", 0xffffffecU},
  };
  const std::array PositiveBranchOperands{
    std::pair<std::string_view, std::uint32_t>{"rs1", 1},
    std::pair<std::string_view, std::uint32_t>{"rs2", 2},
    std::pair<std::string_view, std::uint32_t>{"imm", 8},
  };

  return ExpectDecode (0x003100b3U, "add", AddOperands) &&
         ExpectDecode (0xfff10093U, "addi", AddiOperands) &&
         ExpectDecode (0x00312a23U, "sw", StoreOperands) &&
         ExpectDecode (0xfe208ce3U, "beq", BranchOperands) &&
         ExpectDecode (0xfe312623U, "sw", NegativeStoreOperands) &&
         ExpectDecode (0x00208463U, "beq", PositiveBranchOperands);
}

bool TestImmediateBoundariesAndZeroFields ()
{
  const std::array MinimumI{
    std::pair<std::string_view, std::uint32_t>{"rd", 0},
    std::pair<std::string_view, std::uint32_t>{"rs1", 0},
    std::pair<std::string_view, std::uint32_t>{"imm", 0xfffff800U},
  };
  const std::array MaximumS{
    std::pair<std::string_view, std::uint32_t>{"rs1", 0},
    std::pair<std::string_view, std::uint32_t>{"rs2", 0},
    std::pair<std::string_view, std::uint32_t>{"imm", 2047},
  };
  const std::array MinimumB{
    std::pair<std::string_view, std::uint32_t>{"rs1", 0},
    std::pair<std::string_view, std::uint32_t>{"rs2", 0},
    std::pair<std::string_view, std::uint32_t>{"imm", 0xfffff000U},
  };

  return ExpectDecode (0x80000013U, "addi", MinimumI) &&
         ExpectDecode (0x7e002fa3U, "sw", MaximumS) &&
         ExpectDecode (0x80000063U, "beq", MinimumB);
}

bool TestRecognitionAndUnsupportedWord ()
{
  const auto ChangedRegisters = Decode (0x007302b3U);
  bool Passed = Expect (ChangedRegisters.has_value (), "register bits do not affect add recognition");
  Passed = ExpectDecodeError (
    0xffffffffU,
    riscv::decoder::DecodeError::UnsupportedEncoding,
    "unsupported word should fail"
    ) && Passed;
  return Passed;
}

bool TestDisassembly ()
{
  const std::array<std::byte, 21> Bytes{
    std::byte{0xb3}, std::byte{0x00}, std::byte{0x31}, std::byte{0x00},
    std::byte{0x93}, std::byte{0x00}, std::byte{0xf1}, std::byte{0xff},
    std::byte{0x23}, std::byte{0x2a}, std::byte{0x31}, std::byte{0x00},
    std::byte{0xe3}, std::byte{0x8c}, std::byte{0x20}, std::byte{0xfe},
    std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff},
    std::byte{0xaa},
  };
  const auto Records = riscv::disassembler::Disassemble (Bytes, 0x1000U);

  return Expect (Records.size () == 5, "complete words produce records") &&
         Expect (Records[0].address == 0x1000U, "first address") &&
         Expect (Records[4].address == 0x1010U, "addresses advance by four") &&
         Expect (Records[0].text == "add x1, x2, x3", "add formatting") &&
         Expect (Records[1].text == "addi x1, x2, -1", "addi formatting") &&
         Expect (Records[2].text == "sw x3, 20(x2)", "store formatting") &&
         Expect (Records[3].text == "beq x1, x2, -8", "branch formatting") &&
         Expect (Records[4].text == ".word 0xffffffff", "unknown formatting") &&
         Expect (Records[0].kind == RecordKind::Instruction, "known record status") &&
         Expect (Records[4].kind == RecordKind::UnknownWord, "unknown record status");
}

bool ExpectDisassembly (
  std::uint32_t Word,
  std::string_view Text,
  std::string_view Description
  )
{
  const std::array<std::byte, 4> Bytes{
    std::byte (Word & 0xffU),
    std::byte ((Word >> 8) & 0xffU),
    std::byte ((Word >> 16) & 0xffU),
    std::byte ((Word >> 24) & 0xffU),
  };
  const auto Records = riscv::disassembler::Disassemble (Bytes, 0);

  return Expect (Records.size () == 1, "single word record count") &&
         Expect (Records[0].kind == RecordKind::Instruction, "word is supported") &&
         Expect (Records[0].text == Text, Description);
}

bool TestCanonicalFormatting ()
{
  bool Passed = ExpectDisassembly (0x003100b3U, "add x1, x2, x3", "R format");
  Passed = ExpectDisassembly (0xfff10093U, "addi x1, x2, -1", "I format") && Passed;
  Passed = ExpectDisassembly (0x00312623U, "sw x3, 12(x2)", "S format") && Passed;
  Passed = ExpectDisassembly (0x00208463U, "beq x1, x2, 8", "B format") && Passed;
  Passed = ExpectDisassembly (0x123450b7U, "lui x1, 74565", "U format unsigned") && Passed;
  Passed = ExpectDisassembly (0x008000efU, "jal x1, 8", "J format") && Passed;
  Passed = ExpectDisassembly (0x00008067U, "jalr x0, 0(x1)", "indirect jump") && Passed;
  Passed = ExpectDisassembly (0x023100b3U, "mul x1, x2, x3", "M extension") && Passed;
  Passed = ExpectDisassembly (0x403170b3U, "andn x1, x2, x3", "Zbb extension") && Passed;
  Passed = ExpectDisassembly (0x00100073U, "ebreak", "mnemonic-only format") && Passed;
  return Passed;
}

bool TestFloatingPointFormatting ()
{
  bool Passed = ExpectDisassembly (
    0xffc12087U, "flw f1, -4(x2)", "floating load register classes"
    );
  Passed = ExpectDisassembly (
    0x00312627U, "fsw f3, 12(x2)", "floating store register classes"
    ) && Passed;
  Passed = ExpectDisassembly (
    0xc00110d3U, "fcvt.w.s x1, f2, rtz", "mixed register classes"
    ) && Passed;
  return Passed;
}

bool TestRoundingModesAndFenceSets ()
{
  constexpr std::array<std::pair<std::uint32_t, std::string_view>, 6> Modes{
    std::pair<std::uint32_t, std::string_view>{0, "rne"},
    std::pair<std::uint32_t, std::string_view>{1, "rtz"},
    std::pair<std::uint32_t, std::string_view>{2, "rdn"},
    std::pair<std::uint32_t, std::string_view>{3, "rup"},
    std::pair<std::uint32_t, std::string_view>{4, "rmm"},
    std::pair<std::uint32_t, std::string_view>{7, "dyn"},
  };
  bool Passed = true;

  for (const auto &[Mode, Name] : Modes) {
    std::uint32_t Word = 0x003100d3U | (Mode << 12);
    std::string Expected = "fadd.s f1, f2, f3, ";
    Expected.append (Name);
    Passed = ExpectDisassembly (Word, Expected, "rounding mode") && Passed;
  }

  Passed = ExpectDecodeError (
    0x003150d3U,
    riscv::decoder::DecodeError::IllegalEncoding,
    "rounding mode 5 is reserved"
    ) && Passed;
  Passed = Expect (
    !Decode (0x003160d3U).has_value (), "rounding mode 6 is reserved"
    ) && Passed;

  Passed = ExpectDisassembly (
    0x0b60000fU, "fence irw, or", "canonical fence-set order"
    ) && Passed;
  Passed = Expect (
    !Decode (0x1b60000fU).has_value (), "fence requires zero fm"
    ) && Passed;
  Passed = Expect (
    !Decode (0x0b60800fU).has_value (), "fence requires zero rs1"
    ) && Passed;
  Passed = Expect (
    !Decode (0x0b60008fU).has_value (), "fence requires zero rd"
    ) && Passed;
  return Passed;
}

} // namespace

int main ()
{
  bool Passed = TestRepresentativeInstructions ();
  Passed = TestImmediateBoundariesAndZeroFields () && Passed;
  Passed = TestRecognitionAndUnsupportedWord () && Passed;
  Passed = TestDisassembly () && Passed;
  Passed = TestCanonicalFormatting () && Passed;
  Passed = TestFloatingPointFormatting () && Passed;
  Passed = TestRoundingModesAndFenceSets () && Passed;
  return Passed ? 0 : 1;
}
