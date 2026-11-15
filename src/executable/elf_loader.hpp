#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace riscv::executable {

struct Permissions {
  bool readable;
  bool writable;
  bool executable;
};

class ExecutableImage;
struct ElfLoadError;

[[nodiscard]]
std::expected<ExecutableImage, ElfLoadError> LoadElf (const std::filesystem::path &Path);

class LoadSegment {
public:
  [[nodiscard]] std::uint32_t virtual_address () const noexcept;
  [[nodiscard]] std::uint32_t memory_size () const noexcept;
  [[nodiscard]] Permissions permissions () const noexcept;
  [[nodiscard]] std::span<const std::byte> file_bytes () const noexcept;

private:
  friend std::expected<ExecutableImage, ElfLoadError>
  LoadElf (const std::filesystem::path &Path);

  LoadSegment (
    std::uint32_t VirtualAddress,
    std::uint32_t MemorySize,
    Permissions Access,
    std::vector<std::byte> FileBytes
    );

  std::uint32_t VirtualAddress_;
  std::uint32_t MemorySize_;
  Permissions Access_;
  std::vector<std::byte> FileBytes_;
};

class ExecutableImage {
public:
  [[nodiscard]] std::uint32_t entry_point () const noexcept;
  [[nodiscard]] std::span<const LoadSegment> segments () const noexcept;

private:
  friend std::expected<ExecutableImage, ElfLoadError>
  LoadElf (const std::filesystem::path &Path);

  ExecutableImage (
    std::uint32_t EntryPoint,
    std::vector<LoadSegment> Segments
    );

  std::uint32_t EntryPoint_;
  std::vector<LoadSegment> Segments_;
};

enum class ElfLoadErrorCode {
  io,
  malformed_elf,
  unsupported_format,
  invalid_load_map,
};

struct ElfLoadError {
  ElfLoadErrorCode code;
  std::string message;
};

} // namespace riscv::executable
