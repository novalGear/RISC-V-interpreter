#include "cpu/cpu.hpp"

#include "cpu/execution_binding.hpp"
#include "decoder/decoder.hpp"
#include "machine/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <stdexcept>

namespace riscv::cpu {
namespace {

constexpr std::uint32_t InstructionSize = 4;

Trap MemoryTrap (
  const machine::MemoryFault &Fault,
  std::uint32_t Pc,
  TrapCause MisalignedCause,
  TrapCause AccessCause
  )
{
  TrapCause Cause = Fault.Kind == machine::MemoryFaultKind::Misaligned
                      ? MisalignedCause
                      : AccessCause;
  return Trap{Cause, Pc, Fault.Address};
}

std::optional<Trap> MisalignedTargetTrap (std::uint32_t Target, std::uint32_t Pc)
{
  if ((Target & (InstructionSize - 1)) != 0) {
    return Trap{TrapCause::InstructionAddressMisaligned, Pc, Target};
  }

  return std::nullopt;
}

} // namespace

Cpu::Cpu (machine::Memory &Memory) noexcept
  : Memory_ (Memory)
{
}

std::uint32_t Cpu::Register (std::size_t Index) const
{
  if (Index >= Registers_.size ()) {
    throw std::out_of_range ("RISC-V integer register index must be less than 32");
  }

  return Index == 0 ? 0 : Registers_[Index];
}

void Cpu::SetRegister (std::size_t Index, std::uint32_t Value)
{
  if (Index >= Registers_.size ()) {
    throw std::out_of_range ("RISC-V integer register index must be less than 32");
  }

  if (Index != 0) {
    Registers_[Index] = Value;
  }
}

std::uint32_t Cpu::Pc () const noexcept
{
  return Pc_;
}

void Cpu::SetPc (std::uint32_t Value) noexcept
{
  Pc_ = Value;
}

std::expected<void, Trap> Cpu::Step ()
{
  std::expected<std::uint32_t, machine::MemoryFault> Fetched = Memory_.Read32 (Pc_);

  if (!Fetched) {
    return std::unexpected (MemoryTrap (
      Fetched.error (),
      Pc_,
      TrapCause::InstructionAddressMisaligned,
      TrapCause::InstructionAccessFault
      ));
  }

  std::expected<decoder::DecodedInstruction, decoder::DecodeError> Decoded =
    decoder::Decode (*Fetched);
  if (!Decoded) {
    return std::unexpected (Trap{TrapCause::IllegalInstruction, Pc_, std::nullopt});
  }

  const internal::ExecutionBinding &Binding = internal::ExecutionBindingFor (Decoded->Id ());
  std::span<const decoder::DecodedOperand> Operands = Decoded->Operands ();
  std::uint32_t NextPc = Pc_ + InstructionSize;

  switch (Binding.Kind) {
  case internal::ExecutionKind::Add: {
    std::uint32_t Result = Register (Operands[1].value) + Register (Operands[2].value);
    SetRegister (Operands[0].value, Result);
    break;
  }
  case internal::ExecutionKind::Addi: {
    std::uint32_t Result = Register (Operands[1].value) + Operands[2].value;
    SetRegister (Operands[0].value, Result);
    break;
  }
  case internal::ExecutionKind::Lw: {
    std::uint32_t Address = Register (Operands[1].value) + Operands[2].value;
    std::expected<std::uint32_t, machine::MemoryFault> Value = Memory_.Read32 (Address);

    if (!Value) {
      return std::unexpected (MemoryTrap (
        Value.error (), Pc_, TrapCause::LoadAddressMisaligned, TrapCause::LoadAccessFault
        ));
    }
    SetRegister (Operands[0].value, *Value);
    break;
  }
  case internal::ExecutionKind::Sw: {
    std::uint32_t Address = Register (Operands[0].value) + Operands[2].value;
    std::uint32_t Value = Register (Operands[1].value);
    std::expected<void, machine::MemoryFault> Stored = Memory_.Write32 (Address, Value);

    if (!Stored) {
      return std::unexpected (MemoryTrap (
        Stored.error (), Pc_, TrapCause::StoreAddressMisaligned, TrapCause::StoreAccessFault
        ));
    }
    break;
  }
  case internal::ExecutionKind::Beq:
    if (Register (Operands[0].value) == Register (Operands[1].value)) {
      NextPc = Pc_ + Operands[2].value;
      if (std::optional<Trap> TargetTrap = MisalignedTargetTrap (NextPc, Pc_)) {
        return std::unexpected (*TargetTrap);
      }
    }
    break;
  case internal::ExecutionKind::Jal: {
    std::uint32_t Target = Pc_ + Operands[1].value;

    if (std::optional<Trap> TargetTrap = MisalignedTargetTrap (Target, Pc_)) {
      return std::unexpected (*TargetTrap);
    }
    SetRegister (Operands[0].value, NextPc);
    NextPc = Target;
    break;
  }
  case internal::ExecutionKind::Unbound:
    return std::unexpected (Trap{TrapCause::IllegalInstruction, Pc_, std::nullopt});
  }

  Pc_ = NextPc;
  return {};
}

} // namespace riscv::cpu
