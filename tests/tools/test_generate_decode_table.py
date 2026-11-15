from __future__ import annotations

import copy
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
GENERATOR_PATH = ROOT / "tools" / "decode_table" / "generate.py"
SPEC = importlib.util.spec_from_file_location("decode_table_generate", GENERATOR_PATH)
assert SPEC is not None and SPEC.loader is not None
generate = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = generate
SPEC.loader.exec_module(generate)


FIELDS = """\
"rd",11,7
"rs1",19,15
"rs2",24,20
"imm12",31,20
"imm12hi",31,25
"imm12lo",11,7
"bimm12hi",31,25
"bimm12lo",11,7
"""

UPSTREAM = {
    "add": {
        "encoding": "0000000----------000-----0110011",
        "variable_fields": ["rd", "rs1", "rs2"],
        "extension": ["rv_i"],
        "match": "0x33",
        "mask": "0xfe00707f",
    },
    "addi": {
        "encoding": "-----------------000-----0010011",
        "variable_fields": ["rd", "rs1", "imm12"],
        "extension": ["rv_i"],
        "match": "0x13",
        "mask": "0x707f",
    },
    "beq": {
        "encoding": "-----------------000-----1100011",
        "variable_fields": ["bimm12hi", "rs1", "rs2", "bimm12lo"],
        "extension": ["rv_i"],
        "match": "0x63",
        "mask": "0x707f",
    },
    "sw": {
        "encoding": "-----------------010-----0100011",
        "variable_fields": ["imm12hi", "rs1", "rs2", "imm12lo"],
        "extension": ["rv_i"],
        "match": "0x2023",
        "mask": "0x707f",
    },
}


class GeneratorFixture(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.instructions = self.directory / "instr_dict.json"
        self.fields = self.directory / "arg_lut.csv"
        self.recipes = self.directory / "operand_recipes.json"
        self.output = self.directory / "generated" / "decode_table.cpp"
        self.instructions.write_text(json.dumps(UPSTREAM), encoding="utf-8")
        self.fields.write_text(FIELDS, encoding="utf-8")
        recipes = json.loads(
            (ROOT / "tools" / "decode_table" / "operand_recipes.json").read_text(
                encoding="utf-8"
            )
        )
        recipes["instructions"] = {
            name: recipes["instructions"][name] for name in UPSTREAM
        }
        self.write_recipes(recipes)

    def write_recipes(self, recipes: dict) -> None:
        self.recipes.write_text(json.dumps(recipes), encoding="utf-8")

    def recipe_data(self) -> dict:
        return json.loads(self.recipes.read_text(encoding="utf-8"))

    def build(self):
        return generate.build_table(
            generate.load_json(self.instructions),
            generate.load_fields(self.fields),
            generate.load_json(self.recipes),
        )

    def assert_rejected(self, recipes: dict, message: str) -> None:
        self.write_recipes(recipes)
        with self.assertRaisesRegex(generate.GenerationError, message):
            self.build()


class GenerateDecodeTableTests(GeneratorFixture):
    def test_lowers_selected_layouts_and_orders_instructions(self) -> None:
        table = self.build()

        self.assertEqual([item.name for item in table], ["add", "addi", "beq", "sw"])
        branch_immediate = table[2].operands[2]
        self.assertEqual(branch_immediate.name, "imm")
        self.assertEqual(branch_immediate.width, 13)
        self.assertEqual(branch_immediate.interpretation, "Signed")
        self.assertEqual(
            [
                (piece.source_lsb, piece.width, piece.destination_lsb)
                for piece in branch_immediate.pieces
            ],
            [(31, 1, 12), (7, 1, 11), (25, 6, 5), (8, 4, 1)],
        )
        self.assertEqual(table[0].display_mnemonic, "add")
        self.assertEqual(
            [(token.kind, token.text, token.operand_index) for token in table[3].format_tokens],
            [
                ("Mnemonic", "", 0),
                ("Literal", " ", 0),
                ("Operand", "", 1),
                ("Literal", ", ", 0),
                ("Operand", "", 2),
                ("Literal", "(", 0),
                ("Operand", "", 0),
                ("Literal", ")", 0),
            ],
        )

    def test_output_is_deterministic_and_unchanged_output_is_not_replaced(self) -> None:
        first = generate.generate(self.instructions, self.fields, self.recipes)
        second = generate.generate(self.instructions, self.fields, self.recipes)

        self.assertEqual(first.encode(), second.encode())
        self.assertTrue(generate.write_if_changed(self.output, first))
        original_inode = self.output.stat().st_ino
        self.assertFalse(generate.write_if_changed(self.output, second))
        self.assertEqual(self.output.stat().st_ino, original_inode)
        self.assertEqual(self.output.read_text(encoding="utf-8"), first)
        self.assertLess(first.index('"add"'), first.index('"addi"'))
        self.assertNotIn("Operation", first)
        self.assertIn('#include "decoder/decode_table.hpp"', first)
        self.assertIn("namespace riscv::decoder", first)
        self.assertIn("InstructionTable () noexcept", first)
        self.assertIn("InstructionLegalityFor (", first)
        self.assertIn("InstructionFormatFor (decoder::InstructionId Id) noexcept", first)
        self.assertIn("constexpr std::array<InstructionLegality, 4>", first)
        self.assertIn("constexpr std::array<disassembler::InstructionFormat, 4>", first)

    def test_rejects_unknown_field(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["operands"][0]["pieces"][0]["field"] = "missing"
        self.assert_rejected(recipes, "unknown field 'missing'")

    def test_rejects_bad_field_slice(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["operands"][0]["pieces"][0]["width"] = 6
        self.assert_rejected(recipes, "slice exceeds field 'rd' width 5")

    def test_rejects_overlapping_destination_pieces(self) -> None:
        recipes = self.recipe_data()
        immediate = recipes["layouts"]["store"]["operands"][2]
        immediate["pieces"][1]["destination_lsb"] = 4
        self.assert_rejected(recipes, "overlapping destination pieces")

    def test_rejects_duplicate_operand_names(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["operands"][1]["name"] = "rd"
        self.assert_rejected(recipes, "duplicate operand name 'rd'")

    def test_rejects_more_operands_than_inline_decoder_storage(self) -> None:
        recipes = self.recipe_data()
        extra_operands = copy.deepcopy(recipes["layouts"]["r"]["operands"][0])
        extra_operands["name"] = "extra"
        recipes["layouts"]["r"]["operands"].extend(
            [copy.deepcopy(extra_operands) for _ in range(3)]
        )
        for index, operand in enumerate(recipes["layouts"]["r"]["operands"][3:]):
            operand["name"] = f"extra{index}"
        self.assert_rejected(recipes, "decoder supports at most 5")

    def test_rejects_missing_recipe_coverage(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["i"]["operands"].pop()
        self.assert_rejected(recipes, "do not account for variable source bit 20")

    def test_rejects_ambiguous_patterns(self) -> None:
        upstream = copy.deepcopy(UPSTREAM)
        upstream["addi_copy"] = copy.deepcopy(upstream["addi"])
        self.instructions.write_text(json.dumps(upstream), encoding="utf-8")
        recipes = self.recipe_data()
        recipes["instructions"]["addi_copy"] = {
            "layout": "i",
            "display_mnemonic": "addi",
        }
        self.assert_rejected(recipes, "ambiguous recognition patterns.*'addi'.*'addi_copy'")

    def test_rejects_mask_that_disagrees_with_encoding(self) -> None:
        upstream = copy.deepcopy(UPSTREAM)
        upstream["add"]["mask"] = "0x707f"
        self.instructions.write_text(json.dumps(upstream), encoding="utf-8")
        with self.assertRaisesRegex(generate.GenerationError, "disagree with its encoding"):
            self.build()

    def test_rejects_unsupported_extension(self) -> None:
        upstream = copy.deepcopy(UPSTREAM)
        upstream["add"]["extension"] = ["rv64_i"]
        self.instructions.write_text(json.dumps(upstream), encoding="utf-8")
        with self.assertRaisesRegex(generate.GenerationError, "unsupported extension.*rv64_i"):
            self.build()

    def test_rejects_duplicate_json_keys(self) -> None:
        duplicate = self.directory / "duplicate.json"
        duplicate.write_text('{"schema_version": 1, "schema_version": 1}', encoding="utf-8")
        with self.assertRaisesRegex(generate.GenerationError, "duplicate JSON key"):
            generate.load_json(duplicate)

    def test_failed_validation_does_not_replace_existing_output(self) -> None:
        self.output.parent.mkdir()
        self.output.write_text("keep me", encoding="utf-8")
        recipes = self.recipe_data()
        recipes["instructions"]["unknown"] = {
            "layout": "r",
            "display_mnemonic": "unknown",
        }
        self.write_recipes(recipes)

        with self.assertRaises(generate.GenerationError):
            content = generate.generate(self.instructions, self.fields, self.recipes)
            generate.write_if_changed(self.output, content)

        self.assertEqual(self.output.read_text(encoding="utf-8"), "keep me")

    def test_rejects_unknown_format_operand(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["format_tokens"][2]["operand"] = "missing"
        self.assert_rejected(recipes, "references unknown operand 'missing'")

    def test_rejects_unformatted_operand(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["format_tokens"] = recipes["layouts"]["r"][
            "format_tokens"
        ][:5]
        self.assert_rejected(recipes, "does not reference operand 'rs2'")

    def test_rejects_ignored_field_that_is_also_used(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["intentionally_ignored_fields"] = ["rd"]
        self.assert_rejected(recipes, "ignored field 'rd' overlaps used source bit")

    def test_rejects_constraint_on_used_operand_field(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["field_constraints"] = [
            {"field": "rd", "value": 0}
        ]
        self.assert_rejected(recipes, "constrained field 'rd' overlaps used source bit")

    def test_rejects_constraint_on_non_variable_field(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["field_constraints"] = [
            {"field": "imm12", "value": 0}
        ]
        self.assert_rejected(recipes, "constrains non-variable field 'imm12'")

    def test_rejects_constraint_value_outside_field_width(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["field_constraints"] = [
            {"field": "rd", "value": 32}
        ]
        self.assert_rejected(recipes, "constraint for field 'rd'.*does not fit width 5")

    def test_rejects_duplicate_field_constraints(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["field_constraints"] = [
            {"field": "rd", "value": 0},
            {"field": "rd", "value": 1},
        ]
        self.assert_rejected(recipes, "repeats a constrained field")

    def test_rejects_unknown_constrained_field(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["field_constraints"] = [
            {"field": "missing", "value": 0}
        ]
        self.assert_rejected(recipes, "constrains unknown field 'missing'")

    def test_rejects_unknown_recipe_metadata(self) -> None:
        recipes = self.recipe_data()
        recipes["layouts"]["r"]["surprise"] = True
        self.assert_rejected(recipes, "layout 'r' has unknown field 'surprise'")

    def test_complete_checked_in_profile_builds(self) -> None:
        opcodes = ROOT / "third_party" / "riscv-opcodes"
        environment = os.environ.copy()
        environment["PYTHONPATH"] = str(opcodes / "src")
        subprocess.run(
            [
                sys.executable,
                "-m",
                "riscv_opcodes",
                "rv_i",
                "rv32_i",
                "rv_m",
                "rv_f",
                "rv_zbb",
                "rv32_zbb",
            ],
            cwd=self.directory,
            env=environment,
            check=True,
            capture_output=True,
            text=True,
        )
        full_table = generate.build_table(
            generate.load_json(self.directory / "instr_dict.json"),
            generate.load_fields(opcodes / "arg_lut.csv"),
            generate.load_json(ROOT / "tools" / "decode_table" / "operand_recipes.json"),
        )

        self.assertEqual(len(full_table), 92)
        by_name = {instruction.name: instruction for instruction in full_table}
        expected_names = set(
            "add addi and andi auipc beq bge bgeu blt bltu bne ebreak ecall fence "
            "jal jalr lb lbu lh lhu lui lw or ori sb sh sll slli slt slti sltiu "
            "sltu sra srai srl srli sub sw xor xori".split()
        )
        expected_names.update(
            "mul mulh mulhsu mulhu div divu rem remu".split()
        )
        expected_names.update(
            "flw fsw fmadd_s fmsub_s fnmsub_s fnmadd_s fadd_s fsub_s fmul_s "
            "fdiv_s fsqrt_s fsgnj_s fsgnjn_s fsgnjx_s fmin_s fmax_s fcvt_w_s "
            "fcvt_wu_s fmv_x_w feq_s flt_s fle_s fclass_s fcvt_s_w fcvt_s_wu "
            "fmv_w_x".split()
        )
        expected_names.update(
            "andn orn xnor clz ctz cpop max maxu min minu sext_b sext_h rol ror "
            "orc_b zext_h_rv32 rev8_rv32 rori_rv32".split()
        )
        self.assertEqual(set(by_name), expected_names)
        self.assertEqual(by_name["fadd_s"].display_mnemonic, "fadd.s")
        self.assertEqual(by_name["rev8_rv32"].display_mnemonic, "rev8")
        self.assertEqual(by_name["zext_h_rv32"].display_mnemonic, "zext.h")
        self.assertEqual(by_name["flw"].operands[0].interpretation, "FloatingRegisterIndex")
        self.assertEqual(by_name["fadd_s"].operands[3].interpretation, "RoundingMode")
        self.assertEqual(by_name["fence"].operands[0].interpretation, "FenceSet")
        self.assertEqual(by_name["fence"].legality_mask, 0xF00F8F80)
        self.assertEqual(by_name["fence"].legality_match, 0)


if __name__ == "__main__":
    unittest.main()
