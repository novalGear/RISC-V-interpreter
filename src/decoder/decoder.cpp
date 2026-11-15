#include "decoder/decoder.hpp"

#include "decoder/generated_metadata.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace riscv::decoder {
namespace {

std::optional<std::size_t> FindDescriptor (std::uint32_t Raw)
{
  std::span<const InstructionDescriptor> Table = InstructionTable ();

  for (std::size_t Index = 0; Index < Table.size (); ++Index) {
    const InstructionDescriptor &Descriptor = Table[Index];

    if ((Raw & Descriptor.mask) == Descriptor.match) {
      return Index;
    }
  }

  return std::nullopt;
}

std::uint64_t LowBitsMask (unsigned Width)
{
  if (Width == std::numeric_limits<std::uint64_t>::digits) {
    return std::numeric_limits<std::uint64_t>::max ();
  }

  return (std::uint64_t{1} << Width) - 1;
}

std::uint32_t ExtractOperand (
  std::uint32_t Raw,
  const OperandDescriptor &Descriptor
  )
{
  std::uint64_t Value = 0;

  for (const BitPiece &Piece : Descriptor.pieces) {
    std::uint64_t Bits =
      (static_cast<std::uint64_t> (Raw) >> Piece.source_lsb) &
      LowBitsMask (Piece.width);
    Value |= Bits << Piece.destination_lsb;
  }

  if (Descriptor.interpretation == Interpretation::Signed) {
    std::uint64_t SignBit = std::uint64_t{1} << (Descriptor.width - 1);

    if ((Value & SignBit) != 0) {
      Value |= ~LowBitsMask (Descriptor.width);
    }
  }

  return static_cast<std::uint32_t> (Value);
}

bool IsLegalOperand (Interpretation Kind, std::uint32_t Value)
{
  return Kind != Interpretation::RoundingMode ||
         (Value != 5 && Value != 6);
}

bool IsLegalEncoding (std::size_t DescriptorIndex, std::uint32_t Raw)
{
  const InstructionLegality &Legality = InstructionLegalityFor (DescriptorIndex);

  return (Raw & Legality.Mask) == Legality.Match;
}

} // namespace

std::expected<DecodedInstruction, DecodeError> Decode (std::uint32_t Raw)
{
  std::optional<std::size_t> DescriptorIndex = FindDescriptor (Raw);
  if (!DescriptorIndex) {
    return std::unexpected (DecodeError::UnsupportedEncoding);
  }

  if (!IsLegalEncoding (*DescriptorIndex, Raw)) {
    return std::unexpected (DecodeError::IllegalEncoding);
  }

  const InstructionDescriptor &Descriptor = InstructionTable ()[*DescriptorIndex];
  std::array<DecodedOperand, DecodedInstruction::MaxOperandCount> Operands{};
  std::size_t OperandCount = 0;

  for (const OperandDescriptor &Operand : Descriptor.operands) {
    std::uint32_t Value = ExtractOperand (Raw, Operand);
    if (!IsLegalOperand (Operand.interpretation, Value)) {
      return std::unexpected (DecodeError::IllegalEncoding);
    }
    Operands[OperandCount] = DecodedOperand{
      .name = Operand.name,
      .interpretation = Operand.interpretation,
      .value = Value,
    };
    ++OperandCount;
  }

  return DecodedInstruction (
    InstructionId (*DescriptorIndex),
    std::move (Operands),
    OperandCount
    );
}

InstructionId::InstructionId (std::size_t Ordinal) noexcept
  : Ordinal_ (Ordinal)
{
}

DecodedInstruction::DecodedInstruction (
  InstructionId NewId,
  std::array<DecodedOperand, MaxOperandCount> NewOperands,
  std::size_t OperandCount
  )
  : Id_ (NewId),
    Operands_ (std::move (NewOperands)),
    OperandCount_ (OperandCount)
{
}

InstructionId DecodedInstruction::Id () const noexcept
{
  return Id_;
}

std::span<const DecodedOperand> DecodedInstruction::Operands () const noexcept
{
  return std::span<const DecodedOperand> (Operands_).first (OperandCount_);
}

const InstructionDescriptor &Describe (InstructionId Id) noexcept
{
  return InstructionTable ()[Id.Ordinal_];
}

} // namespace riscv::decoder
