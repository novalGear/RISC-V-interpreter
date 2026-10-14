# RISC-V encoding description format

An encoding description is declarative data consumed by the decoder generator. It
describes how an instruction is recognized and how its operands are extracted.
Instruction behavior is intentionally outside this format.

## Bit numbering

Bit ranges use the RISC-V manual's inclusive `high:low` convention:

```text
bits[6:0]
```

This expression contains seven bits. Bit 0 is the least-significant bit of the
instruction word. Reversed and out-of-range bit ranges are invalid.

## Instruction declaration

```text
extension rv32i

instruction add {
    encoding {
        bits[6:0]   = 0b0110011
        bits[14:12] = 0b000
        bits[31:25] = 0b0000000
    }

    operands {
        rd:  reg = bits[11:7]
        rs1: reg = bits[19:15]
        rs2: reg = bits[24:20]
    }
}
```

Each file declares one architectural extension. An instruction has two sections:

- `encoding` contains fixed bits used to recognize the instruction.
- `operands` contains typed values extracted after the instruction matches.

Names are case-sensitive. Instruction and operand names use lower-case ASCII
identifiers. Whitespace is insignificant. A `#` starts a line comment.
Operand names must be unique within an instruction.

## Encoding rules

Each encoding assignment has a bit range on the left and a binary literal of
the same width on the right. Encoding ranges within one instruction may not
overlap.

All fixed assignments are matched simultaneously. For example, `add` matches
only when its opcode, `funct3`, and `funct7` assignments all match. Bits not
listed in `encoding` may contain operand data or architecturally ignored data.

Two instruction declarations may not accept the same instruction word. The
validator must reject overlapping encodings rather than relying on declaration
order.

## Operand types

```text
reg       architectural integer-register identifier
simm<N>   signed N-bit immediate
uimm<N>   unsigned N-bit immediate
bits<N>   uninterpreted N-bit value
```

The extraction expression width must equal `N`. A `reg` expression must be
exactly five bits for RV32I. Signedness belongs to the operand declaration, not
to the bit-selection expression. This keeps sign extension in generated decoder
code and out of its consumers.

The encoding declaration owns the instruction's operand schema. Semantics use
these names and types but do not redeclare them. A separate operand catalog
would create a third source of truth without hiding additional complexity.

## Operand expressions

A contiguous operand uses a bit selection directly:

```text
imm: simm<12> = bits[31:20]
```

`concat` assembles split fields from most-significant part to least-significant
part:

```text
imm: simm<13> = concat(
    bits[31],
    bits[7],
    bits[30:25],
    bits[11:8],
    0b0
)
```

A single-bit selection uses `bits[N]`. Binary literals are permitted in operand
expressions and retain their written width. Nested `concat` expressions are not
needed and are not part of the initial format.

## RV32I immediate layouts

RV32I has no generic `imm8` or `imm16` encoding fields. Its logical immediates
are reconstructed from the instruction formats:

| Format | Logical value |
| --- | --- |
| I | `bits[31:20]` as `simm<12>` |
| S | `concat(bits[31:25], bits[11:7])` as `simm<12>` |
| B | `concat(bits[31], bits[7], bits[30:25], bits[11:8], 0b0)` as `simm<13>` |
| U | `concat(bits[31:12], 0b000000000000)` as `bits<32>` |
| J | `concat(bits[31], bits[19:12], bits[20], bits[30:21], 0b0)` as `simm<21>` |

The trailing zero in branch and jump immediates is part of the decoded logical
value. Consumers do not need to know how these immediates were distributed in
the instruction word.

## Scope

The initial description covers 32-bit RV32I instructions only. Extensions such
as M, A, C, Zicsr, and Zifencei belong in separate description files. Adding
those extensions must not change the meaning of declarations in
`encodings/rv32i.enc.txt`.
