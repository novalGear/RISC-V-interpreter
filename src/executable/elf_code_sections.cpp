#include "executable/elf_code_sections.hpp"

#include <cstring>
#include <limits>
#include <utility>

#include <elfio/elfio.hpp>

namespace riscv::executable {

CodeSection::CodeSection (
  std::string Name,
  std::uint32_t Address,
  std::vector<std::byte> Bytes
  )
  : Name_ (std::move (Name)),
    Address_ (Address),
    Bytes_ (std::move (Bytes))
{
}

std::string_view CodeSection::name () const noexcept
{
  return Name_;
}

std::uint32_t CodeSection::address () const noexcept
{
  return Address_;
}

std::span<const std::byte> CodeSection::bytes () const noexcept
{
  return Bytes_;
}

std::expected<std::vector<CodeSection>, ElfLoadError>
LoadElfCodeSections (const std::filesystem::path &Path)
{
  ELFIO::elfio Elf;
  if (!Elf.load (Path.string ())) {
    return std::unexpected (
      ElfLoadError{
        ElfLoadErrorCode::malformed_elf,
        "cannot inspect ELF sections",
      }
      );
  }

  if (Elf.get_class () != ELFIO::ELFCLASS32 ||
      Elf.get_encoding () != ELFIO::ELFDATA2LSB ||
      Elf.get_machine () != ELFIO::EM_RISCV) {
    return std::unexpected (
      ElfLoadError{
        ElfLoadErrorCode::unsupported_format,
        "only ELF32 little-endian RISC-V files can be disassembled",
      }
      );
  }

  std::vector<CodeSection> Result;
  for (const auto &Section : Elf.sections) {
    if (Section->get_type () != ELFIO::SHT_PROGBITS ||
        (Section->get_flags () & ELFIO::SHF_EXECINSTR) == 0 ||
        Section->get_size () == 0) {
      continue;
    }

    std::uint64_t Address = Section->get_address ();
    std::uint64_t Size = Section->get_size ();
    constexpr std::uint64_t AddressSpaceSize = std::uint64_t{1} << 32;
    if (Address >= AddressSpaceSize ||
        Size > std::numeric_limits<std::size_t>::max () ||
        Size > AddressSpaceSize - Address) {
      return std::unexpected (
        ElfLoadError{
          ElfLoadErrorCode::invalid_load_map,
          "executable section does not fit the RV32 address space",
        }
        );
    }

    const char *Data = Section->get_data ();
    if (Data == nullptr) {
      return std::unexpected (
        ElfLoadError{
          ElfLoadErrorCode::malformed_elf,
          "cannot read executable section contents",
        }
        );
    }

    std::vector<std::byte> Bytes (static_cast<std::size_t> (Size));
    std::memcpy (Bytes.data (), Data, Bytes.size ());
    Result.emplace_back (
      CodeSection (
        Section->get_name (),
        static_cast<std::uint32_t> (Address),
        std::move (Bytes)
        )
      );
  }

  if (Result.empty ()) {
    return std::unexpected (
      ElfLoadError{
        ElfLoadErrorCode::unsupported_format,
        "ELF executable has no executable sections",
      }
      );
  }

  return Result;
}

} // namespace riscv::executable
