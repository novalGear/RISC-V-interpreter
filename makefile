CXX ?= g++
AR ?= ar
PYTHON ?= python3

CPPFLAGS := -Isrc -Ithird_party/elfio -Ithird_party/spdlog/include
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
GENERATED_DIR := $(BUILD_DIR)/generated/riscv
UPSTREAM_INSTRUCTIONS := $(GENERATED_DIR)/instr_dict.json
GENERATED_TABLE := $(GENERATED_DIR)/decode_table.cpp
EXECUTABLE_LIBRARY := $(BUILD_DIR)/libexecutable.a
DECODER_LIBRARY := $(BUILD_DIR)/libdecoder.a
CPU_LIBRARY := $(BUILD_DIR)/libcpu.a
LOADER_OBJECT := $(BUILD_DIR)/elf_loader.o
CODE_SECTIONS_OBJECT := $(BUILD_DIR)/elf_code_sections.o
DECODER_OBJECT := $(BUILD_DIR)/decoder.o
DISASSEMBLER_OBJECT := $(BUILD_DIR)/disassembler.o
GENERATED_TABLE_OBJECT := $(BUILD_DIR)/decode_table.o
MEMORY_OBJECT := $(BUILD_DIR)/memory.o
CPU_OBJECT := $(BUILD_DIR)/cpu.o
EXECUTION_BINDING_OBJECT := $(BUILD_DIR)/execution_binding.o
ELF_TEST_BINARY := $(BUILD_DIR)/elf_loader_test
DECODER_TEST_BINARY := $(BUILD_DIR)/decoder_test
CPU_TEST_BINARY := $(BUILD_DIR)/cpu_test
DISASSEMBLER_BINARY := $(BUILD_DIR)/riscv-disassemble
GENERATOR := tools/decode_table/generate.py
OPERAND_RECIPES := tools/decode_table/operand_recipes.json
OPCODE_PYTHON_SOURCES := $(wildcard third_party/riscv-opcodes/src/riscv_opcodes/*.py)
OPCODE_INPUTS := \
	third_party/riscv-opcodes/extensions/rv_i \
	third_party/riscv-opcodes/extensions/rv32_i \
	third_party/riscv-opcodes/extensions/rv64_i \
	third_party/riscv-opcodes/extensions/rv_m \
	third_party/riscv-opcodes/extensions/rv_f \
	third_party/riscv-opcodes/extensions/rv_zbb \
	third_party/riscv-opcodes/extensions/rv32_zbb \
	third_party/riscv-opcodes/extensions/rv64_zbb \
	third_party/riscv-opcodes/extensions/rv_zbkb \
	third_party/riscv-opcodes/extensions/rv32_zbkb \
	third_party/riscv-opcodes/arg_lut.csv \
	$(OPCODE_PYTHON_SOURCES)
TASK_REPOSITORY ?= third_party/riscv-interpreter-task
TASK_ELFS = $(wildcard \
	$(TASK_REPOSITORY)/bin/echo/test_* \
	$(TASK_REPOSITORY)/bin/exit_code_argv/test_* \
	$(TASK_REPOSITORY)/bin/rv32i/test_* \
	$(TASK_REPOSITORY)/bin/rv32im/test_* \
	$(TASK_REPOSITORY)/bin/rv32imf/test_* \
	$(TASK_REPOSITORY)/bin/rv32imf_zbb/test_* \
	$(TASK_REPOSITORY)/bin/algo/* \
	$(TASK_REPOSITORY)/bin/rec/*)

.PHONY: all clean test test-python test-task-elfs test-task-elfs-ready

all: $(EXECUTABLE_LIBRARY) $(DECODER_LIBRARY) $(CPU_LIBRARY) $(DISASSEMBLER_BINARY)

$(BUILD_DIR):
	mkdir -p $@

$(GENERATED_DIR):
	mkdir -p $@

$(UPSTREAM_INSTRUCTIONS): $(OPCODE_INPUTS) | $(GENERATED_DIR)
	cd $(GENERATED_DIR) && \
		PYTHONPATH=$(abspath third_party/riscv-opcodes/src) \
		$(PYTHON) -m riscv_opcodes \
			rv_i rv32_i rv_m rv_f rv_zbb rv32_zbb

$(GENERATED_TABLE): $(UPSTREAM_INSTRUCTIONS) $(GENERATOR) $(OPERAND_RECIPES) \
	third_party/riscv-opcodes/arg_lut.csv | $(GENERATED_DIR)
	$(PYTHON) $(GENERATOR) \
		--instructions $(UPSTREAM_INSTRUCTIONS) \
		--fields third_party/riscv-opcodes/arg_lut.csv \
		--recipes $(OPERAND_RECIPES) \
		--output $@

$(LOADER_OBJECT): src/executable/elf_loader.cpp src/executable/elf_loader.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(CODE_SECTIONS_OBJECT): src/executable/elf_code_sections.cpp \
	src/executable/elf_code_sections.hpp src/executable/elf_loader.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(DECODER_OBJECT): src/decoder/decoder.cpp src/decoder/decoder.hpp \
	src/decoder/decode_table.hpp src/decoder/generated_metadata.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(DISASSEMBLER_OBJECT): src/disassembler/disassembler.cpp \
	src/disassembler/disassembler.hpp src/disassembler/format_table.hpp \
	src/decoder/decoder.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(GENERATED_TABLE_OBJECT): $(GENERATED_TABLE) src/decoder/decode_table.hpp \
	src/decoder/generated_metadata.hpp src/disassembler/format_table.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(MEMORY_OBJECT): src/machine/memory.cpp src/machine/memory.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(CPU_OBJECT): src/cpu/cpu.cpp src/cpu/cpu.hpp src/cpu/execution_binding.hpp \
	src/machine/memory.hpp src/decoder/decoder.hpp src/decoder/decode_table.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(EXECUTION_BINDING_OBJECT): src/cpu/execution_binding.cpp src/cpu/execution_binding.hpp \
	src/decoder/decoder.hpp src/decoder/decode_table.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(EXECUTABLE_LIBRARY): $(LOADER_OBJECT) $(CODE_SECTIONS_OBJECT)
	$(AR) rcs $@ $^

$(DECODER_LIBRARY): $(DECODER_OBJECT) $(DISASSEMBLER_OBJECT) $(GENERATED_TABLE_OBJECT)
	$(AR) rcs $@ $^

$(CPU_LIBRARY): $(MEMORY_OBJECT) $(CPU_OBJECT) $(EXECUTION_BINDING_OBJECT)
	$(AR) rcs $@ $^

$(ELF_TEST_BINARY): tests/executable/elf_loader_test.cpp $(EXECUTABLE_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(EXECUTABLE_LIBRARY) -o $@

$(DECODER_TEST_BINARY): tests/decoder/decoder_test.cpp $(DECODER_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(DECODER_LIBRARY) -o $@

$(CPU_TEST_BINARY): tests/cpu/cpu_test.cpp $(CPU_LIBRARY) $(DECODER_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(CPU_LIBRARY) $(DECODER_LIBRARY) -o $@

$(DISASSEMBLER_BINARY): src/disassemble_main.cpp $(EXECUTABLE_LIBRARY) $(DECODER_LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(EXECUTABLE_LIBRARY) $(DECODER_LIBRARY) -o $@

test-python:
	$(PYTHON) -m unittest -v tests/tools/test_generate_decode_table.py

test: test-python $(ELF_TEST_BINARY) $(DECODER_TEST_BINARY) $(CPU_TEST_BINARY)
	$(ELF_TEST_BINARY)
	$(DECODER_TEST_BINARY)
	$(CPU_TEST_BINARY)

test-task-elfs: $(ELF_TEST_BINARY) $(DISASSEMBLER_BINARY)
	$(MAKE) -C $(TASK_REPOSITORY)
	$(MAKE) test-task-elfs-ready

test-task-elfs-ready: $(ELF_TEST_BINARY) $(DISASSEMBLER_BINARY)
	@test -n "$(strip $(TASK_ELFS))" || \
		{ printf '%s\n' 'no task ELF files found in $(TASK_REPOSITORY)/bin' >&2; exit 1; }
	$(ELF_TEST_BINARY) $(TASK_ELFS)
	@for Elf in $(TASK_ELFS); do \
		$(DISASSEMBLER_BINARY) --strict "$$Elf" || exit 1; \
	done

clean:
	rm -rf $(BUILD_DIR)
