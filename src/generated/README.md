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

Generated executable instruction kinds are dense, zero-based ordinals. Entry
`i` in `InstructionNames` is the canonical mnemonic of executable instruction
kind `i`. The ordering comes from the validated generator model and never from
filesystem discovery order. Ordinal values are internal indexes and are not
stable serialization identifiers.

Entry `i` in `RegisterNames` is the canonical architectural name of register
file index `i`. For RV32I, the table contains `x0` through `x31`. ABI names such
as `zero`, `ra`, and `sp` are aliases and do not replace these architectural
names.

The generator must verify that every executable instruction kind and register
index has exactly one corresponding name. The illegal-instruction dispatch kind
and count or sentinel enum values do not receive table entries.

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

The generator owns the definitions of `riscv::InstrKind` and
`riscv::DecodedInstr`. `InstrKind` uses `std::uint8_t` as its underlying type.
Executable values are dense, zero-based indexes followed by an
illegal-instruction dispatch value. `DecodedInstr` exposes its `kind` and stores
the decoded operands required by the corresponding generated executor. Its
operand-storage representation is a generator implementation detail shared by
generated decoders and executors.

The handwritten engine defines the integration signatures in `src/engine.hpp`:

```cpp
using DecoderFn = DecodedInstr (*)(RawInstr);
using ExecutorFn = void (*)(InterpreterEngine&, const DecodedInstr&);
```

Generated function declarations and dispatch tables must use these aliases.
They must not define competing function signatures.

The generator emits the following interface under `riscv::generated`:

```cpp
inline constexpr std::array<DecoderFn, 128> DecoderTable{
    /* one generated opcode-group decoder per entry */
};
inline constexpr std::array<ExecutorFn, InstrKindCount> ExecutorTable{
    /* one generated executor per instruction kind */
};
```

`DecoderTable` is indexed by the seven-bit major opcode. Every one of its 128
entries must be callable. An unallocated opcode selects an illegal-instruction
decoder rather than a null function pointer.

An opcode-group decoder receives the complete raw instruction. It validates all
remaining fixed fields, determines the final `InstrKind`, reconstructs
immediates, and extracts typed operands. Encodings unsupported within an
otherwise allocated opcode group must also produce illegal-instruction
handling. Declaration order must never resolve overlapping encodings.

`ExecutorTable` is indexed by `DecodedInstr::kind`. It contains exactly one
callable entry for every possible kind, including an illegal-instruction
executor. A generated executor receives the engine and decoded instruction,
reads its operands, and implements the declared semantics through the public
`InterpreterEngine` primitive operations.

The handwritten dispatch shells remain:

```cpp
const auto opcode = static_cast<std::size_t>(raw & 0x7fU);
auto instruction = generated::DecoderTable[opcode](raw);

const auto index = static_cast<std::size_t>(instruction.kind);
generated::ExecutorTable[index](engine, instruction);
```

Generated executors must never access `MachineState`, register storage, memory,
or the program counter directly. Register-zero behavior, byte order, access
checks, environment behavior, and architectural arithmetic details belong to
the handwritten engine primitives.

The expected generated files are:

```text
src/generated/riscv/
├── instruction_kind.hpp
├── decoded_instr.hpp
├── names.hpp
├── dispatch_tables.hpp
├── decode_impl.inc
└── execute_impl.inc
```

`dispatch_tables.hpp` includes the generated implementation fragments before
defining the tables, so `engine.cpp` needs to include only that generated
header. No other handwritten source includes the implementation fragments.

Generated files are an internal integration boundary. Consumers execute code
through `InterpreterEngine`; they do not invoke generated functions directly.
