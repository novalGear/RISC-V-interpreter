#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace riscv::decoder {

struct BitPiece {
  unsigned source_lsb;
  unsigned width;
  unsigned destination_lsb;
};

enum class Interpretation {
  IntegerRegisterIndex,
  FloatingRegisterIndex,
  Unsigned,
  Signed,
  RoundingMode,
  FenceSet,
};

struct OperandDescriptor {
  std::string_view name;
  std::span<const BitPiece> pieces;
  unsigned width;
  Interpretation interpretation;
};

struct InstructionDescriptor {
  std::uint32_t mask;
  std::uint32_t match;
  std::string_view name;
  std::span<const OperandDescriptor> operands;
};

[[nodiscard]] std::span<const InstructionDescriptor> InstructionTable () noexcept;

} // namespace riscv::decoder
