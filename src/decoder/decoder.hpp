#pragma once

#include "decoder/decode_table.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

namespace riscv::decoder {
class InstructionId;
}

namespace riscv::disassembler {
struct InstructionFormat;

const InstructionFormat &InstructionFormatFor (decoder::InstructionId Id) noexcept;
}

namespace riscv::decoder {

enum class DecodeError {
  UnsupportedEncoding,
  IllegalEncoding,
};

class InstructionId {
private:
  friend std::expected<class DecodedInstruction, DecodeError> Decode (std::uint32_t Raw);
  friend const InstructionDescriptor &Describe (InstructionId Id) noexcept;
  friend const disassembler::InstructionFormat &
  disassembler::InstructionFormatFor (InstructionId Id) noexcept;

  explicit InstructionId (std::size_t Ordinal) noexcept;

  std::size_t Ordinal_;
};

struct DecodedOperand {
  std::string_view name;
  Interpretation interpretation;
  std::uint32_t value;
};

class DecodedInstruction {
public:
  DecodedInstruction (const DecodedInstruction &) = default;
  DecodedInstruction (DecodedInstruction &&) noexcept = default;
  DecodedInstruction &operator= (const DecodedInstruction &) = default;
  DecodedInstruction &operator= (DecodedInstruction &&) noexcept = default;

  [[nodiscard]] InstructionId Id () const noexcept;
  [[nodiscard]] std::span<const DecodedOperand> Operands () const noexcept;

private:
  friend std::expected<DecodedInstruction, DecodeError> Decode (std::uint32_t Raw);

  static constexpr std::size_t MaxOperandCount = 5;

  DecodedInstruction (
    InstructionId NewId,
    std::array<DecodedOperand, MaxOperandCount> NewOperands,
    std::size_t OperandCount
    );

  InstructionId Id_;
  std::array<DecodedOperand, MaxOperandCount> Operands_;
  std::size_t OperandCount_;
};

[[nodiscard]]
std::expected<DecodedInstruction, DecodeError> Decode (std::uint32_t Raw);

[[nodiscard]]
const InstructionDescriptor &Describe (InstructionId Id) noexcept;

} // namespace riscv::decoder
