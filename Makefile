CXX ?= g++
AR ?= ar

CPPFLAGS := -Isrc -Ithird_party/elfio -Ithird_party/spdlog/include
CXXFLAGS := -std=c++23 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
LIBRARY := $(BUILD_DIR)/libexecutable.a
LOADER_OBJECT := $(BUILD_DIR)/elf_loader.o
TEST_BINARY := $(BUILD_DIR)/elf_loader_test
TASK_REPOSITORY ?= ../riscv-interpreter-task
TASK_ELFS := $(wildcard \
	$(TASK_REPOSITORY)/bin/echo/test_* \
	$(TASK_REPOSITORY)/bin/exit_code_argv/test_* \
	$(TASK_REPOSITORY)/bin/rv32i/test_* \
	$(TASK_REPOSITORY)/bin/rv32im/test_* \
	$(TASK_REPOSITORY)/bin/rv32imf/test_* \
	$(TASK_REPOSITORY)/bin/rv32imf_zbb/test_* \
	$(TASK_REPOSITORY)/bin/algo/* \
	$(TASK_REPOSITORY)/bin/rec/*)

.PHONY: all clean test test-task-elfs

all: $(LIBRARY)

$(BUILD_DIR):
	mkdir -p $@

$(LOADER_OBJECT): src/executable/elf_loader.cpp src/executable/elf_loader.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(LIBRARY): $(LOADER_OBJECT)
	$(AR) rcs $@ $^

$(TEST_BINARY): tests/executable/elf_loader_test.cpp $(LIBRARY)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< $(LIBRARY) -o $@

test: $(TEST_BINARY)
	$(TEST_BINARY)

test-task-elfs: $(TEST_BINARY)
	$(MAKE) -C $(TASK_REPOSITORY)
	$(TEST_BINARY) $(TASK_ELFS)

clean:
	rm -rf $(BUILD_DIR)
