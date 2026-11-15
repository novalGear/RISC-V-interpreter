#include "disassembler/disassembler.hpp"

#include "decoder/decoder.hpp"
#include "disassembler/format_table.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace riscv::disassembler {
namespace {

using decoder::DecodedInstruction;
using decoder::DecodedOperand;

std::uint32_t ReadWord (std::span<const std::byte, 4> Bytes)
{
  std::uint32_t Word = 0;

  for (unsigned Index = 0; Index < Bytes.size (); ++Index) {
    Word |= std::to_integer<std::uint32_t> (Bytes[Index]) << (Index * 8);
  }

  return Word;
}

void AppendUnsigned (std::string &Text, std::uint32_t Value)
{
  char Buffer[10];
  auto [End, Error] = std::to_chars (Buffer, Buffer + sizeof (Buffer), Value);

  if (Error == std::errc{}) {
    Text.append (Buffer, End);
  }
}

void AppendSigned (std::string &Text, std::uint32_t Value)
{
  std::int64_t SignedValue = Value;

  if ((Value & 0x80000000U) != 0) {
    SignedValue -= std::int64_t{1} << 32;
  }

  char Buffer[11];
  auto [End, Error] = std::to_chars (Buffer, Buffer + sizeof (Buffer), SignedValue);

  if (Error == std::errc{}) {
    Text.append (Buffer, End);
  }
}

void AppendRegister (std::string &Text, char Prefix, std::uint32_t Index)
{
  Text.push_back (Prefix);
  AppendUnsigned (Text, Index);
}

void AppendRoundingMode (std::string &Text, std::uint32_t Value)
{
  constexpr std::string_view Names[]{
    "rne", "rtz", "rdn", "rup", "rmm", "reserved", "reserved", "dyn",
  };

  Text.append (Names[Value]);
}

void AppendFenceSet (std::string &Text, std::uint32_t Value)
{
  constexpr char Names[] = "iorw";
  constexpr std::uint32_t Bits[]{8, 4, 2, 1};

  for (unsigned Index = 0; Index < 4; ++Index) {
    if ((Value & Bits[Index]) != 0) {
      Text.push_back (Names[Index]);
    }
  }
}

void AppendOperand (std::string &Text, const DecodedOperand &Operand)
{
  switch (Operand.interpretation) {
  case decoder::Interpretation::IntegerRegisterIndex:
    AppendRegister (Text, 'x', Operand.value);
    break;
  case decoder::Interpretation::FloatingRegisterIndex:
    AppendRegister (Text, 'f', Operand.value);
    break;
  case decoder::Interpretation::Unsigned:
    AppendUnsigned (Text, Operand.value);
    break;
  case decoder::Interpretation::Signed:
    AppendSigned (Text, Operand.value);
    break;
  case decoder::Interpretation::RoundingMode:
    AppendRoundingMode (Text, Operand.value);
    break;
  case decoder::Interpretation::FenceSet:
    AppendFenceSet (Text, Operand.value);
    break;
  }
}

std::string UnknownWord (std::uint32_t Word)
{
  constexpr char HexDigits[] = "0123456789abcdef";
  std::string Text = ".word 0x00000000";

  for (unsigned Index = 0; Index < 8; ++Index) {
    unsigned Shift = (7 - Index) * 4;
    Text[8 + Index] = HexDigits[(Word >> Shift) & 0x0fU];
  }

  return Text;
}

std::string FormatInstruction (const DecodedInstruction &Instruction)
{
  const InstructionFormat &Format = InstructionFormatFor (Instruction.Id ());
  std::span<const DecodedOperand> Operands = Instruction.Operands ();
  std::string Text;

  for (const FormatToken &Token : Format.Tokens) {
    switch (Token.Kind) {
    case FormatTokenKind::Mnemonic:
      Text.append (Format.Mnemonic);
      break;
    case FormatTokenKind::Literal:
      Text.append (Token.Text);
      break;
    case FormatTokenKind::Operand:
      AppendOperand (Text, Operands[Token.OperandIndex]);
      break;
    }
  }

  return Text;
}

} // namespace

std::vector<DisassemblyRecord> Disassemble (
  std::span<const std::byte> Bytes,
  std::uint32_t BaseAddress
  )
{
  constexpr std::size_t InstructionSize = 4;
  std::vector<DisassemblyRecord> Records;
  Records.reserve (Bytes.size () / InstructionSize);

  for (std::size_t Offset = 0; Offset + InstructionSize <= Bytes.size ();
       Offset += InstructionSize) {
    std::uint32_t Word = ReadWord (
      std::span<const std::byte, InstructionSize> (Bytes.subspan (Offset, InstructionSize))
      );
    std::expected<DecodedInstruction, decoder::DecodeError> Instruction =
      decoder::Decode (Word);
    RecordKind Kind = Instruction ? RecordKind::Instruction : RecordKind::UnknownWord;
    std::string Text = Instruction ? FormatInstruction (*Instruction) : UnknownWord (Word);

    Records.push_back (
      DisassemblyRecord{
        .address = BaseAddress + static_cast<std::uint32_t> (Offset),
        .word = Word,
        .text = std::move (Text),
        .kind = Kind,
      }
      );
  }

  return Records;
}

} // namespace riscv::disassembler
