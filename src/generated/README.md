# Generated ISA sources

This directory contains reproducible C++ generated from the validated ISA
descriptions under `isa/`. Generated files must not be edited manually.

## Names

Each generated ISA namespace owns its instruction and register name arrays and
exposes one `IsaNames` view over them:

```cpp
namespace riscv {

inline constexpr auto InstructionNames =
    std::to_array<std::string_view>({
        "lui",
        "auipc",
        // ...
    });

inline constexpr auto RegisterNames =
    std::to_array<std::string_view>({
        "x0",
        "x1",
        // ...
        "x31",
    });

inline constexpr IsaNames Names{
    InstructionNames,
    RegisterNames,
};

} // namespace riscv
```

The arrays provide static storage for the non-owning spans in `IsaNames`.
Their sizes are properties of the generated ISA configuration, not universal
constants in `IsaNames`.

## Indexing contract

Generated instruction kinds are dense, zero-based ordinals. Entry `i` in
`InstructionNames` is the canonical mnemonic of instruction kind `i`. The
ordering comes from the validated generator model and never from filesystem
discovery order. Ordinal values are internal indexes and are not stable
serialization identifiers.

Entry `i` in `RegisterNames` is the canonical architectural name of register
file index `i`. For RV32I, the table contains `x0` through `x31`. ABI names such
as `zero`, `ra`, and `sp` are aliases and do not replace these architectural
names.

The generator must verify that every valid instruction kind and register index
has exactly one corresponding name. Count or sentinel enum values, if any, do
not receive table entries.

An index may be used only with names generated for the same ISA configuration.
`IsaNames` performs no bounds checking; generated decoding and validated runtime
types establish valid indexes.

## Runtime ISA selection

C++ namespaces are selected at compile time. If multiple ISAs are available at
runtime, configuration code selects their `IsaNames` value once and passes that
view to debugging or diagnostic code:

```cpp
const IsaNames& names_for(Architecture architecture)
{
    switch (architecture) {
    case Architecture::RiscV:
        return riscv::Names;
    case Architecture::Arm:
        return arm::Names;
    }

    std::unreachable();
}
```

Architecture switches belong at this construction seam, not in individual name
lookups. Decoding and execution depend on generated kinds and indexes, never on
the strings in `IsaNames`.

## Decoder and executor

`DecodedInstruction` is a handwritten RISC-V type in
`src/isa/riscv/decoded_instruction.hpp`. Generated decoding and execution code
use it as their shared internal representation.

`InterpreterEngine::decode` and `InterpreterEngine::execute` remain handwritten
dispatch shells. Their declarations belong to the engine interface; separate
public `decode.hpp` and `execute.hpp` files are unnecessary.

The generator emits private implementation fragments:

```text
src/generated/riscv/
├── instruction_kind.hpp
├── registers.hpp
├── names.hpp
├── decode_impl.inc
└── execute_impl.inc
```

`decode_impl.inc` contains operand extraction, immediate reconstruction,
opcode-group decoders, illegal-encoding selection, and the opcode dispatch
table. The handwritten decoder extracts the major opcode and invokes the
selected generated function.

`execute_impl.inc` contains one semantic function per instruction and the
`InstructionKind` dispatch table. The handwritten executor indexes that table
and invokes the selected generated function.

Dispatch tables perform selection; they do not replace instruction behavior.
Decoder leaf functions are generated from encoding declarations, and executor
leaf functions are generated from semantic declarations. Machine state, memory,
exception handling, and semantic primitive implementations remain handwritten.

Generated implementation fragments are included only by their corresponding
`.cpp` files and are not part of the public C++ interface.
