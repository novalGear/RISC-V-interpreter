#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace riscv::disassembler {

enum class RecordKind {
  Instruction,
  UnknownWord,
};

struct DisassemblyRecord {
  std::uint32_t address;
  std::uint32_t word;
  std::string text;
  RecordKind kind;
};

// Only complete RV32 instruction words are emitted; trailing segment bytes are ignored.
[[nodiscard]] std::vector<DisassemblyRecord> Disassemble (
  std::span<const std::byte> Bytes,
  std::uint32_t BaseAddress
  );

} // namespace riscv::disassembler
