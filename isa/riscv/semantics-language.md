# RISC-V semantics language

Semantics files describe architectural effects using a small typed language.
They are not embedded C++. The generator lowers these declarations to calls on
C++ machine primitives, whose implementations own concrete CPU and memory state.
This boundary allows operand and operation validation before C++ compilation.

## Declaration

Each file declares one extension. An instruction refers to an encoding with the
same `(extension, instruction-name)` identity:

```text
extension rv32i

instruction add {
    write_reg(rd, read_reg(rs1) + read_reg(rs2));
}
```

Operand names and types come exclusively from the matching encoding. Semantics
must not redeclare them.

Statements end with semicolons. `let` binds an immutable value for the remainder
of the instruction. `if` conditionally executes a block:

```text
let target = read_pc() + imm;
if read_reg(rs1) == read_reg(rs2) {
    set_next_pc(target);
}
```

## Value model

`word` is an RV32 architectural 32-bit bit vector. Word addition, subtraction,
and left shift wrap modulo 2^32. Bitwise `&`, `|`, and `^` operate on words.
Equality (`==`) and inequality (`!=`) compare bit patterns. Boolean negation is
written `!`. A decimal integer literal that fits in 32 bits denotes a `word`.

Encoding operands enter semantics as follows:

| Encoding type | Semantic value |
| --- | --- |
| `reg` | register reference |
| `simm<N>` | sign-extended `word` |
| `uimm<N>` | zero-extended `word` |
| `bits<32>` | `word` |
| other `bits<N>` | `bits<N>` |

The pure calculation operations are:

```text
signed_less(word, word) -> bool
unsigned_less(word, word) -> bool
word_from_bool(bool) -> word
shift_left(word, word) -> word
shift_right_logical(word, word) -> word
shift_right_arithmetic(word, word) -> word
clear_low_bit(word) -> word
```

Shift operations use the low five bits of their amount. Arithmetic right shift
interprets its first argument as a signed two's-complement value. These rules are
guest semantics and must not depend on host C++ overflow or shift behavior.

## Machine primitive signatures

Machine primitives are the only operations that observe or mutate machine
state. Their C++ implementations are hidden behind these signatures:

```text
read_pc() -> word
read_reg(reg) -> word
write_reg(reg, word) -> unit

load_s8(word) -> word
load_s16(word) -> word
load_u8(word) -> word
load_u16(word) -> word
load_u32(word) -> word
store8(word, word) -> unit
store16(word, word) -> unit
store32(word, word) -> unit

set_next_pc(word) -> unit
memory_fence(bits<4>, bits<4>) -> unit
environment_call() -> unit
breakpoint() -> unit
```

These signatures have one machine-readable definition in the generator's
semantic type environment. The validator uses that definition, and the generator
emits the corresponding C++ interface declarations from it. Handwritten C++
implements those generated declarations; it does not maintain a second signature
registry. This document is the human-readable contract, not a competing source
of executable type information.

Loads and stores perform alignment, address translation, permission checks, and
fault delivery according to the machine configuration. A fault aborts the
remaining instruction effects. Store operations consume only the low bits needed
for their width.

`write_reg` defines writes to register zero as a normal no-op. Instruction
semantics therefore require no `rd != 0` checks.

`set_next_pc` enforces the active instruction-address alignment and raises the
architectural exception when a taken branch or jump selects a misaligned target.

## Ordering and program counter

Statements execute in source order. `let` should capture values before an action
that may modify an aliased register. This matters for instructions such as
`jalr`, where `rd` may equal `rs1`.

The execution wrapper defaults the next program counter to
`old_pc + decoded_instruction.length` before running semantics. That length is
four for every instruction in the current RV32I description. Branches and jumps
override it with `set_next_pc`. Exceptions discard the default advancement and
transfer control according to the execution environment.

The executor must not expose a partially committed instruction after a memory
fault or exception. The concrete machine implementation owns the transaction or
staging mechanism needed to provide that guarantee.
