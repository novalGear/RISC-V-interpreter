#pragma once

#include "executable/elf_loader.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace riscv::executable {

class CodeSection {
public:
  [[nodiscard]] std::string_view name () const noexcept;
  [[nodiscard]] std::uint32_t address () const noexcept;
  [[nodiscard]] std::span<const std::byte> bytes () const noexcept;

private:
  friend std::expected<std::vector<CodeSection>, ElfLoadError>
  LoadElfCodeSections (const std::filesystem::path &Path);

  CodeSection (
    std::string Name,
    std::uint32_t Address,
    std::vector<std::byte> Bytes
    );

  std::string Name_;
  std::uint32_t Address_;
  std::vector<std::byte> Bytes_;
};

[[nodiscard]]
std::expected<std::vector<CodeSection>, ElfLoadError>
LoadElfCodeSections (const std::filesystem::path &Path);

} // namespace riscv::executable
