#include "executable/elf_loader.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using riscv::executable::ElfLoadErrorCode;
using riscv::executable::LoadElf;

constexpr std::size_t elf_header_size = 52;
constexpr std::size_t program_header_size = 32;
constexpr std::size_t segment_offset = 0x100;

void put16(std::vector<std::byte>& bytes,
           std::size_t offset,
           std::uint16_t value)
{
    bytes[offset] = static_cast<std::byte>(value);
    bytes[offset + 1] = static_cast<std::byte>(value >> 8);
}

void put32(std::vector<std::byte>& bytes,
           std::size_t offset,
           std::uint32_t value)
{
    for (std::size_t byte = 0; byte < 4; ++byte) {
        bytes[offset + byte] = static_cast<std::byte>(value >> (byte * 8));
    }
}

std::vector<std::byte> make_elf()
{
    std::vector<std::byte> bytes(segment_offset + 4);
    bytes[0] = std::byte{0x7f};
    bytes[1] = std::byte{'E'};
    bytes[2] = std::byte{'L'};
    bytes[3] = std::byte{'F'};
    bytes[4] = std::byte{1}; // ELF32
    bytes[5] = std::byte{1}; // Little-endian
    bytes[6] = std::byte{1}; // Current ELF version

    put16(bytes, 16, 2);       // ET_EXEC
    put16(bytes, 18, 243);     // EM_RISCV
    put32(bytes, 20, 1);       // Current ELF version
    put32(bytes, 24, 0x10000); // Entry point
    put32(bytes, 28, elf_header_size);
    put16(bytes, 40, elf_header_size);
    put16(bytes, 42, program_header_size);
    put16(bytes, 44, 1);
    put16(bytes, 46, 40); // ELF32 section header size, despite no sections

    const std::size_t ph = elf_header_size;
    put32(bytes, ph, 1);                  // PT_LOAD
    put32(bytes, ph + 4, segment_offset); // File offset
    put32(bytes, ph + 8, 0x10000);        // Virtual address
    put32(bytes, ph + 12, 0x10000);       // Physical address, ignored
    put32(bytes, ph + 16, 4);             // File size
    put32(bytes, ph + 20, 8);             // Memory size, including zero fill
    put32(bytes, ph + 24, 5);             // PF_R | PF_X
    put32(bytes, ph + 28, 0x100);         // Alignment

    bytes[segment_offset] = std::byte{0x13};
    bytes[segment_offset + 1] = std::byte{0x00};
    bytes[segment_offset + 2] = std::byte{0x00};
    bytes[segment_offset + 3] = std::byte{0x00};
    return bytes;
}

class TemporaryElf {
public:
    explicit TemporaryElf(std::span<const std::byte> bytes)
        : path_(std::filesystem::temp_directory_path() /
                "riscv-interpreter-elf-loader-test.elf")
    {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    ~TemporaryElf()
    {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

bool expect(bool condition, std::string_view description)
{
    if (!condition) {
        std::cerr << "FAILED: " << description << '\n';
    }
    return condition;
}

bool test_valid_image()
{
    const auto bytes = make_elf();
    const TemporaryElf file(bytes);
    const auto result = LoadElf(file.path());
    if (!expect(result.has_value(), "valid ELF should load")) {
        return false;
    }

    const auto& image = *result;
    const auto segments = image.segments();
    return expect(image.entry_point() == 0x10000, "entry point") &&
           expect(segments.size() == 1, "one load segment") &&
           expect(segments[0].virtual_address() == 0x10000,
                  "segment virtual address") &&
           expect(segments[0].memory_size() == 8, "segment memory size") &&
           expect(segments[0].file_bytes().size() == 4,
                  "initialized byte count") &&
           expect(segments[0].permissions().readable,
                  "segment is readable") &&
           expect(segments[0].permissions().executable,
                  "segment is executable") &&
           expect(!segments[0].permissions().writable,
                  "segment is not writable");
}

bool test_wrong_machine()
{
    auto bytes = make_elf();
    put16(bytes, 18, 62); // EM_X86_64
    const TemporaryElf file(bytes);
    const auto result = LoadElf(file.path());
    return expect(!result &&
                      result.error().code == ElfLoadErrorCode::unsupported_format,
                  "wrong machine should be unsupported");
}

bool test_invalid_segment_sizes()
{
    auto bytes = make_elf();
    put32(bytes, elf_header_size + 20, 2);
    const TemporaryElf file(bytes);
    const auto result = LoadElf(file.path());
    return expect(!result &&
                      result.error().code == ElfLoadErrorCode::invalid_load_map,
                  "file size larger than memory size should be rejected");
}

bool test_non_executable_entry()
{
    auto bytes = make_elf();
    put32(bytes, elf_header_size + 24, 6); // PF_R | PF_W
    const TemporaryElf file(bytes);
    const auto result = LoadElf(file.path());
    return expect(!result &&
                      result.error().code == ElfLoadErrorCode::invalid_load_map,
                  "entry point must be executable");
}

bool test_missing_file()
{
    const auto result = LoadElf("/definitely/not/an/elf/file");
    return expect(!result && result.error().code == ElfLoadErrorCode::io,
                  "missing file should report an I/O error");
}

bool test_task_elf(const std::filesystem::path& path)
{
    const auto result = LoadElf(path);
    if (!result) {
        std::cerr << "FAILED: " << path << ": " << result.error().message
                  << '\n';
        return false;
    }
    return expect(!result->segments().empty(),
                  "task ELF should contain load segments");
}

} // namespace

int main(int argc, char** argv)
{
    bool passed = test_valid_image();
    passed = test_wrong_machine() && passed;
    passed = test_invalid_segment_sizes() && passed;
    passed = test_non_executable_entry() && passed;
    passed = test_missing_file() && passed;

    for (int argument = 1; argument < argc; ++argument) {
        passed = test_task_elf(argv[argument]) && passed;
    }

    return passed ? 0 : 1;
}
