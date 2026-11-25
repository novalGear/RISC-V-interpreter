#pragma once

#include "machine/memory.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>

namespace riscv::cpu {

enum class TrapCause {
  InstructionAddressMisaligned,
  InstructionAccessFault,
  IllegalInstruction,
  LoadAddressMisaligned,
  LoadAccessFault,
  StoreAddressMisaligned,
  StoreAccessFault,
};

struct Trap {
  TrapCause Cause;
  std::uint32_t Pc;
  std::optional<std::uint32_t> FaultAddress;
};

class Cpu {
public:
  explicit Cpu (machine::Memory &Memory) noexcept;

  [[nodiscard]] std::uint32_t Register (std::size_t Index) const;
  void SetRegister (std::size_t Index, std::uint32_t Value);

  [[nodiscard]] std::uint32_t Pc () const noexcept;
  void SetPc (std::uint32_t Value) noexcept;

  [[nodiscard]] std::expected<void, Trap> Step ();

private:
  static constexpr std::size_t RegisterCount = 32;

  machine::Memory &Memory_;
  std::array<std::uint32_t, RegisterCount> Registers_{};
  std::uint32_t Pc_ = 0;
};

} // namespace riscv::cpu
