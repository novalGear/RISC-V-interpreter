#pragma once

#include <cstddef>
#include <cstdint>

namespace riscv::decoder {

struct InstructionLegality {
  std::uint32_t Mask;
  std::uint32_t Match;
};

[[nodiscard]]
const InstructionLegality &InstructionLegalityFor (std::size_t DescriptorIndex) noexcept;

} // namespace riscv::decoder
