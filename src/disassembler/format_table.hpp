#pragma once

#include "decoder/decoder.hpp"

#include <span>
#include <string_view>

namespace riscv::disassembler {

enum class FormatTokenKind {
  Mnemonic,
  Literal,
  Operand,
};

struct FormatToken {
  FormatTokenKind Kind;
  std::string_view Text;
  unsigned OperandIndex;
};

struct InstructionFormat {
  std::string_view Mnemonic;
  std::span<const FormatToken> Tokens;
};

[[nodiscard]]
const InstructionFormat &InstructionFormatFor (decoder::InstructionId Id) noexcept;

} // namespace riscv::disassembler
