# ISA description validation

Encoding and semantics files form one logical ISA model. Validation joins them
without relying on file-discovery order and reports all independent problems
before generation begins.

## Diagnostics

Validation is a calculation: it returns a report and does not print messages or
terminate the process. The calling command is responsible for rendering the
report and selecting an exit status.

Each diagnostic contains:

- severity: `warning` or `error`;
- a stable diagnostic code;
- source file and location when available;
- extension and instruction identifiers when applicable;
- a concise explanation.

Diagnostics are sorted by source path, location, and diagnostic code so output
is deterministic. Warnings do not prevent generation. Any error prevents
generation and results in a nonzero command exit status.

## Validation phases

Validation proceeds in phases:

1. Parse every encoding and semantics file.
2. Build extension and instruction indexes.
3. Detect duplicate declarations.
4. Join encodings and semantics by instruction identity.
5. Resolve semantic operand references and type-check expressions.
6. Check encoding ranges, widths, and overlaps.
7. Return all collected diagnostics.

Failure in one declaration must not stop validation of independent declarations
or files. A check is skipped only when an earlier error left it without the data
it requires. Diagnostics caused solely by an earlier error are suppressed to
avoid misleading cascades.

Conflicting declarations remain individually checkable when their dependencies
are unambiguous. For example, each duplicate semantics body is still resolved
and type-checked when exactly one matching encoding exists.

The parser should recover at instruction boundaries. It must not fabricate a
partially valid instruction when its identity cannot be established.

## Instruction identity

An instruction is identified by `(extension, instruction-name)`, for example
`(rv32i, add)` or `(m, mul)`. Both encoding and semantics files declare their
extension explicitly. Directory and file names are organizational and do not
determine identity.

The encoding owns the instruction identity and operand schema. Semantics refer
to that schema and never redeclare operand locations, types, or widths.

## File discovery

Encoding sources are direct children of `encodings/` named `<extension>.enc.txt`.
Semantics sources are direct children of `semantics/` named
`<extension>.sem.txt`. The extension declared inside the file must match its
filename. Violations are errors; directory or filename values never silently
override source metadata.

## Required checks

An encoding without semantics produces a warning:

```text
warning[missing-semantics]: rv32i.add has an encoding but no semantics
```

This permits encoding-only tools and incremental development. A generator that
requires executable instructions may promote this warning to an error as a
command policy without changing the validation model.

Semantics without an encoding produce an error because the instruction cannot
be decoded:

```text
error[missing-encoding]: rv32i.add has semantics but no encoding
```

Duplicate encoding, semantics, or operand declarations are errors. Every
conflicting location is reported; declaration order never selects a winner.

```text
error[duplicate-semantics]: multiple semantics declarations for rv32i.add
error[duplicate-operand]: rv32i.add declares operand "rs1" more than once
```

Semantics may reference only operands declared by the corresponding encoding.
Resolution continues after a failed reference so all unknown names are reported.

```text
error[undeclared-operand]: rv32i.add references undeclared operand "rs3"
```

Each extraction expression width must equal its encoding-declared operand width.
Semantic references and operation arguments must type-check against that schema.

```text
error[operand-width-mismatch]: rv32i.add.rd requires 5 bits but extracts 4
error[semantic-type-mismatch]: write_reg expects reg as argument 1
error[unknown-semantic-operation]: unknown operation "write_gpr"
```

Encoding and semantics declarations for an instruction must have the same
extension. For an unmatched declaration, report an extension mismatch only when
exactly one declaration of the opposite role has the same instruction name in a
different extension. In that case, suppress the redundant missing-counterpart
diagnostic for that pair. If there are zero or multiple candidates, report only
the applicable missing-encoding error or missing-semantics warning; the validator
must not guess a pairing.

```text
error[extension-mismatch]: add encoding belongs to rv32i but semantics belongs to m
```

File-layout diagnostics are likewise explicit:

```text
error[invalid-source-name]: expected rv32i.enc.txt for extension rv32i
error[source-extension-mismatch]: rv32i.enc.txt declares extension m
```

## Generation boundary

Code generation receives a validated model, never parser fragments. Generation
starts only when the report has no errors. This boundary prevents every backend
from independently defending against malformed or ambiguous input.
