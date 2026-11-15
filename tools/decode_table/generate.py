#!/usr/bin/env python3
"""Generate a validated C++ RISC-V instruction decoding table."""

from __future__ import annotations

import argparse
import csv
import json
import os
from dataclasses import dataclass
from pathlib import Path
import re
import sys
import tempfile
from typing import Any, Iterable


SCHEMA_VERSION = 3
NAME_RE = re.compile(r"^[a-z][a-z0-9_]*$")
INTERPRETATIONS = {
    "integer_register": "IntegerRegisterIndex",
    "floating_register": "FloatingRegisterIndex",
    "unsigned": "Unsigned",
    "signed": "Signed",
    "rounding_mode": "RoundingMode",
    "fence_set": "FenceSet",
}
TOKEN_KINDS = {"mnemonic": "Mnemonic", "literal": "Literal", "operand": "Operand"}
MAX_DECODED_OPERANDS = 5


class GenerationError(ValueError):
    """An input cannot be represented by the decode-table contract."""


@dataclass(frozen=True)
class Field:
    high: int
    low: int


@dataclass(frozen=True)
class Piece:
    source_lsb: int
    width: int
    destination_lsb: int


@dataclass(frozen=True)
class Operand:
    name: str
    width: int
    interpretation: str
    pieces: tuple[Piece, ...]


@dataclass(frozen=True)
class Instruction:
    name: str
    mask: int
    match: int
    operands: tuple[Operand, ...]
    display_mnemonic: str
    format_tokens: tuple["FormatToken", ...]
    legality_mask: int
    legality_match: int


@dataclass(frozen=True)
class FormatToken:
    kind: str
    text: str
    operand_index: int


def _reject_duplicate_keys(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise GenerationError(f"duplicate JSON key {key!r}")
        result[key] = value
    return result


def load_json(path: Path) -> Any:
    try:
        with path.open(encoding="utf-8") as source:
            return json.load(source, object_pairs_hook=_reject_duplicate_keys)
    except GenerationError:
        raise
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise GenerationError(f"cannot read JSON {path}: {error}") from error


def _object(value: Any, context: str) -> dict[str, Any]:
    if not isinstance(value, dict):
        raise GenerationError(f"{context} must be a JSON object")
    return value


def _list(value: Any, context: str) -> list[Any]:
    if not isinstance(value, list):
        raise GenerationError(f"{context} must be a JSON array")
    return value


def _integer(value: Any, context: str) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise GenerationError(f"{context} must be an integer")
    return value


def _name(value: Any, context: str) -> str:
    if not isinstance(value, str) or not NAME_RE.fullmatch(value):
        raise GenerationError(f"{context} must be a lowercase canonical name")
    return value


def _required(mapping: dict[str, Any], key: str, context: str) -> Any:
    if key not in mapping:
        raise GenerationError(f"{context} is missing required field {key!r}")
    return mapping[key]


def _only_keys(mapping: dict[str, Any], allowed: set[str], context: str) -> None:
    unknown = sorted(set(mapping) - allowed)
    if unknown:
        raise GenerationError(f"{context} has unknown field {unknown[0]!r}")


def load_fields(path: Path) -> dict[str, Field]:
    fields: dict[str, Field] = {}
    try:
        with path.open(newline="", encoding="utf-8") as source:
            for line_number, row in enumerate(csv.reader(source), 1):
                if not row or (len(row) == 1 and not row[0].strip()):
                    continue
                if len(row) != 3:
                    raise GenerationError(
                        f"{path}:{line_number}: expected field, high, low"
                    )
                name = row[0].strip()
                if not NAME_RE.fullmatch(name):
                    raise GenerationError(
                        f"{path}:{line_number}: invalid field name {name!r}"
                    )
                if name in fields:
                    raise GenerationError(
                        f"{path}:{line_number}: duplicate field {name!r}"
                    )
                try:
                    high, low = int(row[1]), int(row[2])
                except ValueError as error:
                    raise GenerationError(
                        f"{path}:{line_number}: field {name!r} has non-integer bounds"
                    ) from error
                if not 0 <= low <= high < 32:
                    raise GenerationError(
                        f"{path}:{line_number}: field {name!r} has invalid bounds "
                        f"[{high}:{low}]"
                    )
                fields[name] = Field(high, low)
    except GenerationError:
        raise
    except (OSError, UnicodeError, csv.Error) as error:
        raise GenerationError(f"cannot read fields {path}: {error}") from error
    if not fields:
        raise GenerationError(f"fields file {path} is empty")
    return fields


def _parse_u32(value: Any, context: str) -> int:
    if isinstance(value, bool):
        raise GenerationError(f"{context} must be an integer or hexadecimal string")
    if not isinstance(value, (int, str)):
        raise GenerationError(f"{context} must be an integer or hexadecimal string")
    try:
        parsed = int(value, 0) if isinstance(value, str) else int(value)
    except (TypeError, ValueError) as error:
        raise GenerationError(f"{context} must be an integer or hexadecimal string") from error
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise GenerationError(f"{context} does not fit in 32 bits")
    return parsed


def _encoding_pattern(value: Any, context: str) -> tuple[str, int, int]:
    if not isinstance(value, str) or len(value) != 32 or set(value) - {"0", "1", "-"}:
        raise GenerationError(f"{context} must be a 32-character 0/1/- string")
    mask = 0
    match = 0
    for bit, character in enumerate(reversed(value)):
        if character != "-":
            mask |= 1 << bit
        if character == "1":
            match |= 1 << bit
    return value, mask, match


def _parse_upstream_entry(
    name: str, raw: Any, allowed_extensions: frozenset[str]
) -> tuple[int, int, str, tuple[str, ...]]:
    context = f"instruction {name!r}"
    entry = _object(raw, context)
    encoding, encoding_mask, encoding_match = _encoding_pattern(
        _required(entry, "encoding", context), f"{context} encoding"
    )
    mask = _parse_u32(_required(entry, "mask", context), f"{context} mask")
    match = _parse_u32(_required(entry, "match", context), f"{context} match")
    if mask != encoding_mask or match != encoding_match:
        raise GenerationError(f"{context} mask/match disagree with its encoding")
    if match & mask != match:
        raise GenerationError(f"{context} has match bits outside its mask")

    raw_variables = _list(
        _required(entry, "variable_fields", context), f"{context} variable_fields"
    )
    variables = tuple(_name(item, f"{context} variable field") for item in raw_variables)
    if len(set(variables)) != len(variables):
        raise GenerationError(f"{context} has duplicate variable fields")

    raw_extensions = _list(
        _required(entry, "extension", context), f"{context} extension"
    )
    if not raw_extensions or any(not isinstance(item, str) for item in raw_extensions):
        raise GenerationError(f"{context} extension must contain strings")
    unsupported = sorted(set(raw_extensions) - allowed_extensions)
    if unsupported:
        raise GenerationError(
            f"{context} belongs to unsupported extension(s): {', '.join(unsupported)}"
        )
    return mask, match, encoding, variables


def _lower_operand(
    instruction_name: str,
    raw: Any,
    fields: dict[str, Field],
    variables: set[str],
    encoding: str,
    used_source_bits: set[int],
) -> Operand:
    context = f"instruction {instruction_name!r} operand"
    recipe = _object(raw, context)
    _only_keys(recipe, {"name", "width", "interpretation", "pieces"}, context)
    name = _name(_required(recipe, "name", context), f"{context} name")
    context = f"instruction {instruction_name!r} operand {name!r}"
    width = _integer(_required(recipe, "width", context), f"{context} width")
    if not 1 <= width <= 32:
        raise GenerationError(f"{context} width must be between 1 and 32")
    interpretation_name = _required(recipe, "interpretation", context)
    if interpretation_name not in INTERPRETATIONS:
        raise GenerationError(
            f"{context} has unknown interpretation {interpretation_name!r}"
        )
    if interpretation_name in {"integer_register", "floating_register"} and width != 5:
        raise GenerationError(f"{context} register width must be 5")
    if interpretation_name == "rounding_mode" and width != 3:
        raise GenerationError(f"{context} rounding-mode width must be 3")
    if interpretation_name == "fence_set" and width != 4:
        raise GenerationError(f"{context} fence-set width must be 4")

    raw_pieces = _list(_required(recipe, "pieces", context), f"{context} pieces")
    if not raw_pieces:
        raise GenerationError(f"{context} must contain at least one piece")
    pieces: list[Piece] = []
    destination_bits: set[int] = set()
    for piece_index, raw_piece in enumerate(raw_pieces):
        piece_context = f"{context} piece {piece_index}"
        piece = _object(raw_piece, piece_context)
        _only_keys(
            piece,
            {"field", "field_lsb", "width", "destination_lsb"},
            piece_context,
        )
        field_name = _name(
            _required(piece, "field", piece_context), f"{piece_context} field"
        )
        if field_name not in fields:
            raise GenerationError(f"{piece_context} references unknown field {field_name!r}")
        if field_name not in variables:
            raise GenerationError(
                f"{piece_context} field {field_name!r} is not a variable field of "
                f"instruction {instruction_name!r}"
            )
        field_lsb = _integer(
            _required(piece, "field_lsb", piece_context), f"{piece_context} field_lsb"
        )
        piece_width = _integer(
            _required(piece, "width", piece_context), f"{piece_context} width"
        )
        destination_lsb = _integer(
            _required(piece, "destination_lsb", piece_context),
            f"{piece_context} destination_lsb",
        )
        field = fields[field_name]
        field_width = field.high - field.low + 1
        if piece_width <= 0:
            raise GenerationError(f"{piece_context} width must be positive")
        if field_lsb < 0 or field_lsb + piece_width > field_width:
            raise GenerationError(
                f"{piece_context} slice exceeds field {field_name!r} width {field_width}"
            )
        if destination_lsb < 0 or destination_lsb + piece_width > width:
            raise GenerationError(f"{piece_context} does not fit operand width {width}")

        source_lsb = field.low + field_lsb
        source_range = set(range(source_lsb, source_lsb + piece_width))
        destination_range = set(range(destination_lsb, destination_lsb + piece_width))
        if destination_bits & destination_range:
            raise GenerationError(f"{context} has overlapping destination pieces")
        duplicate_sources = used_source_bits & source_range
        if duplicate_sources:
            raise GenerationError(
                f"{piece_context} reuses instruction source bit {min(duplicate_sources)}"
            )
        fixed_sources = [bit for bit in source_range if encoding[31 - bit] != "-"]
        if fixed_sources:
            raise GenerationError(
                f"{piece_context} source bit {min(fixed_sources)} conflicts with recognition bits"
            )
        destination_bits.update(destination_range)
        used_source_bits.update(source_range)
        pieces.append(Piece(source_lsb, piece_width, destination_lsb))

    if interpretation_name == "signed" and width - 1 not in destination_bits:
        raise GenerationError(f"{context} does not supply signed bit {width - 1}")
    return Operand(name, width, INTERPRETATIONS[interpretation_name], tuple(pieces))


def _lower_format_tokens(
    instruction_name: str,
    raw_tokens: Any,
    operands: list[Operand],
) -> tuple[FormatToken, ...]:
    context = f"instruction {instruction_name!r} format_tokens"
    tokens = _list(raw_tokens, context)
    if not tokens:
        raise GenerationError(f"{context} must not be empty")
    operand_indexes = {operand.name: index for index, operand in enumerate(operands)}
    used_operands: set[str] = set()
    result: list[FormatToken] = []
    mnemonic_count = 0
    for index, raw_token in enumerate(tokens):
        token_context = f"{context} token {index}"
        token = _object(raw_token, token_context)
        kind = _required(token, "kind", token_context)
        if kind not in TOKEN_KINDS:
            raise GenerationError(f"{token_context} has unknown kind {kind!r}")
        if kind == "mnemonic":
            _only_keys(token, {"kind"}, token_context)
            mnemonic_count += 1
            result.append(FormatToken(TOKEN_KINDS[kind], "", 0))
        elif kind == "literal":
            _only_keys(token, {"kind", "text"}, token_context)
            text = _required(token, "text", token_context)
            if not isinstance(text, str) or not text:
                raise GenerationError(f"{token_context} text must be a non-empty string")
            result.append(FormatToken(TOKEN_KINDS[kind], text, 0))
        else:
            _only_keys(token, {"kind", "operand"}, token_context)
            operand_name = _name(
                _required(token, "operand", token_context), f"{token_context} operand"
            )
            if operand_name not in operand_indexes:
                raise GenerationError(
                    f"{token_context} references unknown operand {operand_name!r}"
                )
            if operand_name in used_operands:
                raise GenerationError(
                    f"{context} repeats operand {operand_name!r}"
                )
            used_operands.add(operand_name)
            result.append(FormatToken(TOKEN_KINDS[kind], "", operand_indexes[operand_name]))
    if result[0].kind != "Mnemonic" or mnemonic_count != 1:
        raise GenerationError(f"{context} must contain one leading mnemonic token")
    missing_operands = sorted(set(operand_indexes) - used_operands)
    if missing_operands:
        raise GenerationError(
            f"{context} does not reference operand {missing_operands[0]!r}"
        )
    return tuple(result)


def build_table(
    upstream_raw: Any, fields: dict[str, Field], recipes_raw: Any
) -> tuple[Instruction, ...]:
    upstream = _object(upstream_raw, "instruction dictionary")
    recipes = _object(recipes_raw, "recipes")
    schema_version = _integer(
        _required(recipes, "schema_version", "recipes"), "recipes schema_version"
    )
    if schema_version != SCHEMA_VERSION:
        raise GenerationError(
            f"unsupported recipe schema_version {schema_version}; expected {SCHEMA_VERSION}"
        )
    target = _required(recipes, "target", "recipes")
    if not isinstance(target, str) or not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", target):
        raise GenerationError("recipes target must be a simple non-empty identifier")
    raw_extensions = _list(
        _required(recipes, "upstream_extensions", "recipes"),
        "recipes upstream_extensions",
    )
    if not raw_extensions or any(not isinstance(item, str) or not item for item in raw_extensions):
        raise GenerationError("recipes upstream_extensions must contain non-empty strings")
    allowed_extensions = frozenset(raw_extensions)
    if len(allowed_extensions) != len(raw_extensions):
        raise GenerationError("recipes upstream_extensions contains duplicates")

    layouts = _object(_required(recipes, "layouts", "recipes"), "recipes layouts")
    selections = _object(
        _required(recipes, "instructions", "recipes"), "recipes instructions"
    )
    if not selections:
        raise GenerationError("recipes instructions must not be empty")
    _only_keys(
        recipes,
        {"schema_version", "target", "upstream_extensions", "layouts", "instructions"},
        "recipes",
    )

    instructions: list[Instruction] = []
    for raw_name in sorted(selections):
        name = _name(raw_name, "selected instruction name")
        if name not in upstream:
            raise GenerationError(f"unknown selected instruction {name!r}")
        selection = _object(selections[name], f"instruction {name!r} selection")
        _only_keys(selection, {"layout", "display_mnemonic"}, f"instruction {name!r} selection")
        layout_name = _required(selection, "layout", f"instruction {name!r} selection")
        if not isinstance(layout_name, str) or layout_name not in layouts:
            raise GenerationError(
                f"instruction {name!r} references unknown layout {layout_name!r}"
            )
        display_mnemonic = _required(
            selection, "display_mnemonic", f"instruction {name!r} selection"
        )
        if not isinstance(display_mnemonic, str) or not re.fullmatch(
            r"[a-z][a-z0-9.]*", display_mnemonic
        ):
            raise GenerationError(
                f"instruction {name!r} display_mnemonic must be canonical syntax"
            )
        mask, match, encoding, variable_names = _parse_upstream_entry(
            name, upstream[name], allowed_extensions
        )
        variables = set(variable_names)
        expected_source_bits: set[int] = set()
        for field_name in variable_names:
            if field_name not in fields:
                raise GenerationError(
                    f"instruction {name!r} references unknown upstream field {field_name!r}"
                )
            field = fields[field_name]
            expected_source_bits.update(range(field.low, field.high + 1))
            fixed_bits = [
                bit
                for bit in range(field.low, field.high + 1)
                if encoding[31 - bit] != "-"
            ]
            if fixed_bits:
                raise GenerationError(
                    f"instruction {name!r} variable field {field_name!r} overlaps "
                    f"recognition bit {min(fixed_bits)}"
                )
        encoding_variable_bits = {
            bit for bit in range(32) if encoding[31 - bit] == "-"
        }
        if expected_source_bits != encoding_variable_bits:
            differing_bit = min(expected_source_bits ^ encoding_variable_bits)
            raise GenerationError(
                f"instruction {name!r} variable_fields disagree with encoding at "
                f"bit {differing_bit}"
            )

        layout = _object(layouts[layout_name], f"layout {layout_name!r}")
        _only_keys(
            layout,
            {
                "operands",
                "format_tokens",
                "intentionally_ignored_fields",
                "field_constraints",
            },
            f"layout {layout_name!r}",
        )
        raw_operands = _list(
            _required(layout, "operands", f"layout {layout_name!r}"),
            f"layout {layout_name!r} operands",
        )
        if len(raw_operands) > MAX_DECODED_OPERANDS:
            raise GenerationError(
                f"instruction {name!r} has {len(raw_operands)} operands; decoder supports "
                f"at most {MAX_DECODED_OPERANDS}"
            )
        operand_names: set[str] = set()
        used_source_bits: set[int] = set()
        operands: list[Operand] = []
        for raw_operand in raw_operands:
            operand = _lower_operand(
                name, raw_operand, fields, variables, encoding, used_source_bits
            )
            if operand.name in operand_names:
                raise GenerationError(
                    f"instruction {name!r} has duplicate operand name {operand.name!r}"
                )
            operand_names.add(operand.name)
            operands.append(operand)

        constraint_context = f"layout {layout_name!r} field_constraints"
        raw_constraints = _list(layout.get("field_constraints", []), constraint_context)
        constraints: list[tuple[str, int]] = []
        for constraint_index, raw_constraint in enumerate(raw_constraints):
            item_context = f"{constraint_context} item {constraint_index}"
            constraint = _object(raw_constraint, item_context)
            _only_keys(constraint, {"field", "value"}, item_context)
            field_name = _name(
                _required(constraint, "field", item_context), f"{item_context} field"
            )
            value = _integer(
                _required(constraint, "value", item_context), f"{item_context} value"
            )
            constraints.append((field_name, value))

        constraint_fields = [field_name for field_name, _ in constraints]
        if len(set(constraint_fields)) != len(constraint_fields):
            raise GenerationError(f"{constraint_context} repeats a constrained field")

        legality_mask = 0
        legality_match = 0
        for field_name, value in constraints:
            if field_name not in fields:
                raise GenerationError(
                    f"instruction {name!r} constrains unknown field {field_name!r}"
                )
            if field_name not in variables:
                raise GenerationError(
                    f"instruction {name!r} constrains non-variable field {field_name!r}"
                )
            field = fields[field_name]
            field_width = field.high - field.low + 1
            if value < 0 or value >= (1 << field_width):
                raise GenerationError(
                    f"instruction {name!r} constraint for field {field_name!r} does "
                    f"not fit width {field_width}"
                )
            field_mask = ((1 << field_width) - 1) << field.low
            if field_mask & mask:
                raise GenerationError(
                    f"instruction {name!r} constraint for field {field_name!r} "
                    "overlaps recognition bits"
                )
            overlap = used_source_bits & set(range(field.low, field.high + 1))
            if overlap:
                raise GenerationError(
                    f"instruction {name!r} constrained field {field_name!r} overlaps "
                    f"used source bit {min(overlap)}"
                )
            if legality_mask & field_mask:
                raise GenerationError(
                    f"instruction {name!r} constraint for field {field_name!r} "
                    "overlaps another constrained field"
                )
            legality_mask |= field_mask
            legality_match |= value << field.low
            used_source_bits.update(range(field.low, field.high + 1))

        raw_ignored = _list(
            _required(layout, "intentionally_ignored_fields", f"layout {layout_name!r}"),
            f"layout {layout_name!r} intentionally_ignored_fields",
        )
        ignored_fields = [
            _name(item, f"layout {layout_name!r} ignored field") for item in raw_ignored
        ]
        if len(set(ignored_fields)) != len(ignored_fields):
            raise GenerationError(f"layout {layout_name!r} repeats an ignored field")
        for field_name in ignored_fields:
            if field_name not in variables:
                raise GenerationError(
                    f"instruction {name!r} ignores non-variable field {field_name!r}"
                )
            field = fields[field_name]
            ignored_bits = set(range(field.low, field.high + 1))
            overlap = used_source_bits & ignored_bits
            if overlap:
                raise GenerationError(
                    f"instruction {name!r} ignored field {field_name!r} overlaps used "
                    f"source bit {min(overlap)}"
                )
            used_source_bits.update(ignored_bits)

        missing_bits = sorted(expected_source_bits - used_source_bits)
        if missing_bits:
            raise GenerationError(
                f"instruction {name!r} recipes do not account for variable source "
                f"bit {missing_bits[0]}"
            )
        format_tokens = _lower_format_tokens(
            name,
            _required(layout, "format_tokens", f"layout {layout_name!r}"),
            operands,
        )
        instructions.append(
            Instruction(
                name,
                mask,
                match,
                tuple(operands),
                display_mnemonic,
                format_tokens,
                legality_mask,
                legality_match,
            )
        )

    for index, first in enumerate(instructions):
        for second in instructions[index + 1 :]:
            if ((first.match ^ second.match) & (first.mask & second.mask)) == 0:
                raise GenerationError(
                    f"ambiguous recognition patterns for {first.name!r} and {second.name!r}"
                )
    return tuple(instructions)


def _cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render_cpp(instructions: Iterable[Instruction], target: str) -> str:
    table = tuple(instructions)
    lines = [
        "// Generated by tools/decode_table/generate.py; do not edit.",
        f"// Generator schema: {SCHEMA_VERSION}; target: {target}",
        '#include "decoder/decode_table.hpp"',
        '#include "decoder/generated_metadata.hpp"',
        '#include "disassembler/format_table.hpp"',
        "",
        "#include <array>",
        "#include <span>",
        "",
        "namespace riscv::decoder {",
        "namespace {",
        "",
    ]
    for instruction_index, instruction in enumerate(table):
        for operand_index, operand in enumerate(instruction.operands):
            piece_name = f"kInstruction{instruction_index}Operand{operand_index}Pieces"
            lines.append(
                f"constexpr std::array<BitPiece, {len(operand.pieces)}> {piece_name}{{{{"
            )
            for piece in operand.pieces:
                lines.append(
                    "  BitPiece{"
                    f"{piece.source_lsb}u, {piece.width}u, {piece.destination_lsb}u"
                    "},"
                )
            lines.extend(["}};", ""])
        operand_name = f"kInstruction{instruction_index}Operands"
        lines.append(
            f"constexpr std::array<OperandDescriptor, {len(instruction.operands)}> "
            f"{operand_name}{{{{"
        )
        for operand_index, operand in enumerate(instruction.operands):
            piece_name = f"kInstruction{instruction_index}Operand{operand_index}Pieces"
            lines.append(
                "  OperandDescriptor{"
                f"{_cpp_string(operand.name)}, {piece_name}, {operand.width}u, "
                f"Interpretation::{operand.interpretation}"
                "},"
            )
        lines.extend(["}};", ""])

        token_name = f"kInstruction{instruction_index}FormatTokens"
        lines.append(
            f"constexpr std::array<disassembler::FormatToken, {len(instruction.format_tokens)}> "
            f"{token_name}{{{{"
        )
        for token in instruction.format_tokens:
            lines.append(
                "  disassembler::FormatToken{"
                f"disassembler::FormatTokenKind::{token.kind}, {_cpp_string(token.text)}, "
                f"{token.operand_index}u"
                "},"
            )
        lines.extend(["}};", ""])

    lines.append(
        f"constexpr std::array<InstructionDescriptor, {len(table)}> kInstructions{{{{"
    )
    for instruction_index, instruction in enumerate(table):
        lines.append(
            "  InstructionDescriptor{"
            f"0x{instruction.mask:08x}u, 0x{instruction.match:08x}u, "
            f"{_cpp_string(instruction.name)}, kInstruction{instruction_index}Operands"
            "},"
        )
    lines.extend(
        [
            "}};",
            "",
            f"constexpr std::array<InstructionLegality, {len(table)}> kInstructionLegalities{{{{",
        ]
    )
    for instruction in table:
        lines.append(
            "  InstructionLegality{"
            f"0x{instruction.legality_mask:08x}u, "
            f"0x{instruction.legality_match:08x}u"
            "},"
        )
    lines.extend(
        [
            "}};",
            "",
            f"constexpr std::array<disassembler::InstructionFormat, {len(table)}> "
            "kInstructionFormats{{",
        ]
    )
    for instruction_index, instruction in enumerate(table):
        lines.append(
            "  disassembler::InstructionFormat{"
            f"{_cpp_string(instruction.display_mnemonic)}, "
            f"kInstruction{instruction_index}FormatTokens"
            "},"
        )
    lines.extend(
        [
            "}};",
            "",
            "static_assert (kInstructions.size () == kInstructionLegalities.size ());",
            "static_assert (kInstructions.size () == kInstructionFormats.size ());",
            "",
            "} // namespace",
            "",
            "std::span<const InstructionDescriptor> InstructionTable () noexcept",
            "{",
            "  return kInstructions;",
            "}",
            "",
            "const InstructionLegality &InstructionLegalityFor (",
            "  std::size_t DescriptorIndex",
            "  ) noexcept",
            "{",
            "  return kInstructionLegalities[DescriptorIndex];",
            "}",
            "",
            "} // namespace riscv::decoder",
            "",
            "namespace riscv::disassembler {",
            "",
            "const InstructionFormat &InstructionFormatFor (decoder::InstructionId Id) noexcept",
            "{",
            "  return decoder::kInstructionFormats[Id.Ordinal_];",
            "}",
            "",
            "} // namespace riscv::disassembler",
            "",
        ]
    )
    return "\n".join(lines)


def write_if_changed(path: Path, content: str) -> bool:
    data = content.encode("utf-8")
    try:
        if path.exists() and path.read_bytes() == data:
            return False
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary_name: str | None = None
        try:
            with tempfile.NamedTemporaryFile(
                mode="wb", dir=path.parent, prefix=f".{path.name}.", delete=False
            ) as temporary:
                temporary_name = temporary.name
                temporary.write(data)
                temporary.flush()
                os.fsync(temporary.fileno())
            os.replace(temporary_name, path)
            temporary_name = None
        finally:
            if temporary_name is not None:
                try:
                    os.unlink(temporary_name)
                except FileNotFoundError:
                    pass
    except OSError as error:
        raise GenerationError(f"cannot write output {path}: {error}") from error
    return True


def generate(instructions_path: Path, fields_path: Path, recipes_path: Path) -> str:
    upstream = load_json(instructions_path)
    fields = load_fields(fields_path)
    recipes = load_json(recipes_path)
    table = build_table(upstream, fields, recipes)
    target = _object(recipes, "recipes")["target"]
    return render_cpp(table, target)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instructions", type=Path, required=True)
    parser.add_argument("--fields", type=Path, required=True)
    parser.add_argument("--recipes", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args(argv)
    try:
        content = generate(arguments.instructions, arguments.fields, arguments.recipes)
        write_if_changed(arguments.output, content)
    except GenerationError as error:
        parser.exit(1, f"error: {error}\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
