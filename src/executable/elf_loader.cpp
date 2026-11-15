#include "executable/elf_loader.hpp"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <utility>

#include <elfio/elfio.hpp>
#include <spdlog/spdlog.h>

namespace riscv::executable {
namespace {

constexpr std::uint64_t GuestAddressSpaceSize = std::uint64_t{1} << 32;

struct DecodedSegment {
  std::uint32_t VirtualAddress;
  std::uint32_t MemorySize;
  Permissions Access;
  std::vector<std::byte> FileBytes;
};

std::unexpected<ElfLoadError>
MakeError (
  ElfLoadErrorCode Code,
  std::string Message
  )
{
  return std::unexpected (ElfLoadError{Code, std::move (Message)});
}

bool IsPowerOfTwo (std::uint64_t Value)
{
  return Value != 0 && (Value & (Value - 1)) == 0;
}

std::expected<std::uint64_t, ElfLoadError>
GetFileSize (
  std::ifstream &Input,
  const std::filesystem::path &Path
  )
{
  std::ifstream::pos_type End = Input.tellg ();

  if (End < 0) {
    return MakeError (
      ElfLoadErrorCode::io,
      "cannot determine ELF file size: " + Path.string ()
      );
  }

  Input.seekg (0, std::ios::beg);
  if (!Input) {
    return MakeError (
      ElfLoadErrorCode::io,
      "cannot seek to the beginning of ELF file: " + Path.string ()
      );
  }

  return static_cast<std::uint64_t> (End);
}

std::expected<void, ElfLoadError>
ReadElf (
  ELFIO::elfio &Elf,
  std::ifstream &Input
  )
{
  if (!Elf.load (Input)) {
    return MakeError (
      ElfLoadErrorCode::malformed_elf,
      "file is not a well-formed ELF file"
      );
  }

  return {};
}

std::expected<void, ElfLoadError> ValidateElfFormat (const ELFIO::elfio &Elf)
{
  if (Elf.get_class () != ELFIO::ELFCLASS32) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "only ELF32 executables are supported"
      );
  }

  if (Elf.get_encoding () != ELFIO::ELFDATA2LSB) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "only little-endian executables are supported"
      );
  }

  if (Elf.get_elf_version () != ELFIO::EV_CURRENT ||
      Elf.get_version () != ELFIO::EV_CURRENT) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "unsupported ELF version"
      );
  }

  if (Elf.get_machine () != ELFIO::EM_RISCV) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "ELF executable is not for RISC-V"
      );
  }

  if (Elf.get_type () != ELFIO::ET_EXEC) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "only fixed-address ELF executables are supported"
      );
  }

  if (Elf.get_flags () != 0) {
    return MakeError (
      ElfLoadErrorCode::unsupported_format,
      "unsupported RISC-V ELF flags"
      );
  }

  for (const auto &Segment : Elf.segments) {
    ELFIO::Elf_Word Type = Segment->get_type ();

    if (Type == ELFIO::PT_INTERP || Type == ELFIO::PT_DYNAMIC) {
      return MakeError (
        ElfLoadErrorCode::unsupported_format,
        "dynamic linking is not supported"
        );
    }

    if (Type == ELFIO::PT_TLS) {
      return MakeError (
        ElfLoadErrorCode::unsupported_format,
        "thread-local storage is not supported"
        );
    }
  }

  return {};
}

std::expected<std::optional<DecodedSegment>, ElfLoadError>
DecodeSegment (
  const ELFIO::segment &Segment,
  std::uint64_t FileSize
  )
{
  std::uint64_t Offset = Segment.get_offset ();
  std::uint64_t VirtualAddress = Segment.get_virtual_address ();
  std::uint64_t FileByteCount = Segment.get_file_size ();
  std::uint64_t MemorySize = Segment.get_memory_size ();
  std::uint64_t Alignment = Segment.get_align ();
  ELFIO::Elf_Word Flags = Segment.get_flags ();
  Permissions Access{
    .readable = (Flags & ELFIO::PF_R) != 0,
    .writable = (Flags & ELFIO::PF_W) != 0,
    .executable = (Flags & ELFIO::PF_X) != 0,
  };

  if (Segment.get_type () != ELFIO::PT_LOAD || MemorySize == 0) {
    return std::optional<DecodedSegment>{};
  }

  if (FileByteCount > MemorySize) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "load segment is smaller in memory than in the file"
      );
  }

  if (Offset > FileSize || FileByteCount > FileSize - Offset) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "load segment lies outside the ELF file"
      );
  }

  if (VirtualAddress >= GuestAddressSpaceSize ||
      MemorySize > std::numeric_limits<std::uint32_t>::max () ||
      MemorySize > GuestAddressSpaceSize - VirtualAddress) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "load segment does not fit the RV32 address space"
      );
  }

  if (Alignment > 1 &&
      (!IsPowerOfTwo (Alignment) || VirtualAddress % Alignment != Offset % Alignment)) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "load segment has invalid alignment"
      );
  }

  if (FileByteCount > std::numeric_limits<std::size_t>::max ()) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "load segment is too large for this host"
      );
  }

  std::vector<std::byte> FileBytes (static_cast<std::size_t> (FileByteCount));
  if (FileByteCount != 0) {
    const char *Source = Segment.get_data ();

    if (Source == nullptr) {
      return MakeError (
        ElfLoadErrorCode::malformed_elf,
        "cannot read load segment contents"
        );
    }

    std::memcpy (FileBytes.data (), Source, FileBytes.size ());
  }

  return DecodedSegment{
    .VirtualAddress = static_cast<std::uint32_t> (VirtualAddress),
    .MemorySize = static_cast<std::uint32_t> (MemorySize),
    .Access = Access,
    .FileBytes = std::move (FileBytes),
  };
}

std::expected<void, ElfLoadError> ValidateLoadMap (std::vector<DecodedSegment> &Segments)
{
  if (Segments.empty ()) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "ELF executable has no loadable memory"
      );
  }

  std::ranges::sort (Segments, {}, &DecodedSegment::VirtualAddress);
  for (std::size_t Index = 1; Index < Segments.size (); ++Index) {
    const DecodedSegment &Previous = Segments[Index - 1];
    const DecodedSegment &Current = Segments[Index];
    std::uint64_t PreviousEnd =
      static_cast<std::uint64_t> (Previous.VirtualAddress) + Previous.MemorySize;

    if (Current.VirtualAddress < PreviousEnd) {
      return MakeError (
        ElfLoadErrorCode::invalid_load_map,
        "load segments overlap in guest memory"
        );
    }
  }

  return {};
}

std::expected<std::uint32_t, ElfLoadError>
ValidateEntryPoint (
  ELFIO::Elf64_Addr Entry,
  std::span<const DecodedSegment> Segments
  )
{
  if (Entry > std::numeric_limits<std::uint32_t>::max ()) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "entry point does not fit the RV32 address space"
      );
  }

  auto EntrySegment = std::ranges::find_if (
    Segments,
    [Entry] (const DecodedSegment &Segment) {
      std::uint64_t End =
        static_cast<std::uint64_t> (Segment.VirtualAddress) + Segment.MemorySize;

      return Segment.Access.executable &&
             Entry >= Segment.VirtualAddress &&
             Entry < End;
    }
    );

  if (EntrySegment == Segments.end () || Entry % 4 != 0) {
    return MakeError (
      ElfLoadErrorCode::invalid_load_map,
      "entry point is not a supported executable address"
      );
  }

  return static_cast<std::uint32_t> (Entry);
}

} // namespace

LoadSegment::LoadSegment (
  std::uint32_t VirtualAddress,
  std::uint32_t MemorySize,
  Permissions Access,
  std::vector<std::byte> FileBytes
  )
  : VirtualAddress_ (VirtualAddress),
    MemorySize_ (MemorySize),
    Access_ (Access),
    FileBytes_ (std::move (FileBytes))
{
}

std::uint32_t LoadSegment::virtual_address () const noexcept
{
  return VirtualAddress_;
}

std::uint32_t LoadSegment::memory_size () const noexcept
{
  return MemorySize_;
}

Permissions LoadSegment::permissions () const noexcept
{
  return Access_;
}

std::span<const std::byte> LoadSegment::file_bytes () const noexcept
{
  return FileBytes_;
}

ExecutableImage::ExecutableImage (
  std::uint32_t EntryPoint,
  std::vector<LoadSegment> Segments
  )
  : EntryPoint_ (EntryPoint),
    Segments_ (std::move (Segments))
{
}

std::uint32_t ExecutableImage::entry_point () const noexcept
{
  return EntryPoint_;
}

std::span<const LoadSegment> ExecutableImage::segments () const noexcept
{
  return Segments_;
}

std::expected<ExecutableImage, ElfLoadError>
LoadElf (const std::filesystem::path &Path)
{
  std::ifstream Input (Path, std::ios::binary | std::ios::ate);
  ELFIO::elfio Elf;
  std::vector<DecodedSegment> DecodedSegments;
  std::vector<LoadSegment> LoadSegments;

  if (!Input) {
    return MakeError (
      ElfLoadErrorCode::io,
      "cannot open ELF file: " + Path.string ()
      );
  }

  std::expected<std::uint64_t, ElfLoadError> FileSize = GetFileSize (Input, Path);
  if (!FileSize) {
    return std::unexpected (std::move (FileSize.error ()));
  }

  std::expected<void, ElfLoadError> LoadResult = ReadElf (Elf, Input);
  if (!LoadResult) {
    return std::unexpected (std::move (LoadResult.error ()));
  }

  std::expected<void, ElfLoadError> FormatResult = ValidateElfFormat (Elf);
  if (!FormatResult) {
    return std::unexpected (std::move (FormatResult.error ()));
  }

  DecodedSegments.reserve (Elf.segments.size ());
  for (const auto &Segment : Elf.segments) {
    std::expected<std::optional<DecodedSegment>, ElfLoadError> Decoded =
      DecodeSegment (*Segment, *FileSize);

    if (!Decoded) {
      return std::unexpected (std::move (Decoded.error ()));
    }

    if (*Decoded) {
      DecodedSegments.push_back (std::move (**Decoded));
    }
  }

  std::expected<void, ElfLoadError> MapResult = ValidateLoadMap (DecodedSegments);
  if (!MapResult) {
    return std::unexpected (std::move (MapResult.error ()));
  }

  std::expected<std::uint32_t, ElfLoadError> EntryPoint =
    ValidateEntryPoint (Elf.get_entry (), DecodedSegments);
  if (!EntryPoint) {
    return std::unexpected (std::move (EntryPoint.error ()));
  }

  LoadSegments.reserve (DecodedSegments.size ());
  for (DecodedSegment &Segment : DecodedSegments) {
    LoadSegments.push_back (
      LoadSegment (
        Segment.VirtualAddress,
        Segment.MemorySize,
        Segment.Access,
        std::move (Segment.FileBytes)
        )
      );
  }

  ExecutableImage Image (*EntryPoint, std::move (LoadSegments));

  spdlog::debug (
    "Loaded ELF '{}': entry=0x{:08x}, segments={}",
    Path.string (),
    Image.entry_point (),
    Image.segments ().size ()
    );

  for (const LoadSegment &Segment : Image.segments ()) {
    Permissions Access = Segment.permissions ();

    spdlog::debug (
      "  segment: address=0x{:08x}, memory={}, file={}, permissions={}{}{}",
      Segment.virtual_address (),
      Segment.memory_size (),
      Segment.file_bytes ().size (),
      Access.readable ? 'r' : '-',
      Access.writable ? 'w' : '-',
      Access.executable ? 'x' : '-'
      );
  }

  return Image;
}

} // namespace riscv::executable
