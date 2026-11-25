#include "machine/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>

namespace riscv::machine {
namespace {

constexpr std::size_t WordSize = 4;

std::expected<std::size_t, MemoryFault> CheckedOffset (
  std::size_t Size,
  std::uint32_t Address
  ) noexcept
{
  std::size_t Offset = Address;

  if ((Address & (WordSize - 1)) != 0) {
    return std::unexpected (MemoryFault{MemoryFaultKind::Misaligned, Address});
  }
  if (Offset > Size || Size - Offset < WordSize) {
    return std::unexpected (MemoryFault{MemoryFaultKind::AccessFault, Address});
  }

  return Offset;
}

} // namespace

Memory::Memory (std::size_t Size)
  : Bytes_ (Size)
{
}

std::expected<std::byte, MemoryFault> Memory::Read8 (std::uint32_t Address) const noexcept
{
  std::size_t Offset = Address;

  if (Offset >= Bytes_.size ()) {
    return std::unexpected (MemoryFault{MemoryFaultKind::AccessFault, Address});
  }

  return Bytes_[Offset];
}

std::expected<std::uint32_t, MemoryFault> Memory::Read32 (
  std::uint32_t Address
  ) const noexcept
{
  std::expected<std::size_t, MemoryFault> Offset = CheckedOffset (Bytes_.size (), Address);
  std::uint32_t Value = 0;

  if (!Offset) {
    return std::unexpected (Offset.error ());
  }

  for (std::size_t Index = 0; Index < WordSize; ++Index) {
    Value |= std::to_integer<std::uint32_t> (Bytes_[*Offset + Index]) << (Index * 8);
  }

  return Value;
}

std::expected<void, MemoryFault> Memory::Write32 (
  std::uint32_t Address,
  std::uint32_t Value
  ) noexcept
{
  std::expected<std::size_t, MemoryFault> Offset = CheckedOffset (Bytes_.size (), Address);

  if (!Offset) {
    return std::unexpected (Offset.error ());
  }

  for (std::size_t Index = 0; Index < WordSize; ++Index) {
    Bytes_[*Offset + Index] = std::byte ((Value >> (Index * 8)) & 0xffU);
  }

  return {};
}

} // namespace riscv::machine
