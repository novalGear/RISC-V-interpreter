#pragma once

#include "decoder/decoder.hpp"

namespace riscv::cpu::internal {

enum class ExecutionKind {
  Unbound,
  Add,
  Addi,
  Lw,
  Sw,
  Beq,
  Jal,
};

struct ExecutionBinding {
  ExecutionKind Kind;
};

const ExecutionBinding &ExecutionBindingFor (decoder::InstructionId Id);

} // namespace riscv::cpu::internal
