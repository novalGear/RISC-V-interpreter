#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <vector>

namespace riscv::machine {

enum class MemoryFaultKind {
  Misaligned,
  AccessFault,
};

struct MemoryFault {
  MemoryFaultKind Kind;
  std::uint32_t Address;
};

class Memory {
public:
  explicit Memory (std::size_t Size);

  [[nodiscard]]
  std::expected<std::byte, MemoryFault> Read8 (std::uint32_t Address) const noexcept;

  [[nodiscard]]
  std::expected<std::uint32_t, MemoryFault> Read32 (std::uint32_t Address) const noexcept;

  [[nodiscard]]
  std::expected<void, MemoryFault> Write32 (
    std::uint32_t Address,
    std::uint32_t Value
    ) noexcept;

private:
  std::vector<std::byte> Bytes_;
};

} // namespace riscv::machine
