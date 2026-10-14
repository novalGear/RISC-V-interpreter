#include "engine.hpp"
#include "generated/riscv/dispatch_tables.hpp"

namespace riscv {

RawInstr InterpreterEngine::fetch() const {
    return load_u32(state_.pc);
}

DecodedInstr InterpreterEngine::decode(RawInstr raw) const {
    const auto opcode = static_cast<std::size_t>(raw & 0x7fU);
    return generated::DecoderTable[opcode](raw);
} 

void InterpreterEngine::execute(const DecodedInstr& instr) {
    const auto index = static_cast<std::size_t>(instr.kind);
    generated::ExecutorTable[index](*this, instr);
}

} // namespace riscv