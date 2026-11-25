#include "cpu/cpu.hpp"
#include "machine/memory.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {

using riscv::cpu::Cpu;
using riscv::cpu::Trap;
using riscv::cpu::TrapCause;
using riscv::machine::Memory;
using riscv::machine::MemoryFault;
using riscv::machine::MemoryFaultKind;

bool Expect (bool Condition, std::string_view Description)
{
  if (!Condition) {
    std::cerr << "FAILED: " << Description << '\n';
  }

  return Condition;
}

bool PutInstruction (Memory &Memory, std::uint32_t Address, std::uint32_t Word)
{
  return Expect (Memory.Write32 (Address, Word).has_value (), "instruction setup");
}

bool ExpectTrap (
  const std::expected<void, Trap> &Result,
  TrapCause Cause,
  std::uint32_t Pc,
  std::string_view Description
  )
{
  if (!Expect (!Result.has_value (), Description)) {
    return false;
  }

  const Trap &Caught = Result.error ();
  return Expect (Caught.Cause == Cause, "trap cause") &&
         Expect (Caught.Pc == Pc, "faulting PC");
}

bool ExpectFaultAddress (
  const std::expected<void, Trap> &Result,
  std::uint32_t Address,
  std::string_view Description
  )
{
  if (!Expect (!Result.has_value (), Description)) {
    return false;
  }

  return Expect (Result.error ().FaultAddress == Address, Description);
}

template<typename Value>
bool ExpectMemoryFault (
  const std::expected<Value, MemoryFault> &Result,
  MemoryFaultKind Kind,
  std::string_view Description
  )
{
  if (!Expect (!Result.has_value (), Description)) {
    return false;
  }

  return Expect (Result.error ().Kind == Kind, Description);
}

bool TestMemoryAccess ()
{
  Memory GuestMemory (8);
  bool Passed = Expect (GuestMemory.Write32 (0, 0x78563412U).has_value (), "write word");
  Passed = Expect (GuestMemory.Read32 (0).value () == 0x78563412U, "little-endian round trip") && Passed;

  const auto Misaligned = GuestMemory.Read32 (2);
  Passed = ExpectMemoryFault (Misaligned, MemoryFaultKind::Misaligned, "misaligned read fails") && Passed;

  const auto OutOfRange = GuestMemory.Write32 (8, 0xffffffffU);
  Passed = ExpectMemoryFault (OutOfRange, MemoryFaultKind::AccessFault, "out-of-range write fails") && Passed;
  Passed = Expect (GuestMemory.Read32 (4).value () == 0, "failed write changes no bytes") && Passed;

  Memory ShortMemory (6);
  const auto PartialWrite = ShortMemory.Write32 (4, 0xaabbccddU);
  Passed = ExpectMemoryFault (
    PartialWrite, MemoryFaultKind::AccessFault, "partial out-of-range write fails"
    ) && Passed;
  Passed = Expect (
    ShortMemory.Read8 (4).value () == std::byte{0},
    "failed write preserves first prefix byte"
    ) && Passed;
  Passed = Expect (
    ShortMemory.Read8 (5).value () == std::byte{0},
    "failed write preserves second prefix byte"
    ) && Passed;
  return Passed;
}

bool TestArithmeticX0AndAliasing ()
{
  Memory GuestMemory (32);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x002080b3U); // add x1, x1, x2
  Passed = PutInstruction (GuestMemory, 4, 0xfff08013U) && Passed; // addi x0, x1, -1
  Passed = PutInstruction (GuestMemory, 8, 0xffe08193U) && Passed; // addi x3, x1, -2
  Passed = PutInstruction (GuestMemory, 12, 0x002001b3U) && Passed; // add x3, x0, x2
  Processor.SetRegister (1, 0xffffffffU);
  Processor.SetRegister (2, 2);

  Passed = Expect (Processor.Step ().has_value (), "aliased add executes") && Passed;
  Passed = Expect (Processor.Register (1) == 1, "add wraps and reads before write") && Passed;
  Passed = Expect (Processor.Pc () == 4, "sequential PC advances") && Passed;
  Passed = Expect (Processor.Step ().has_value (), "addi to x0 executes") && Passed;
  Passed = Expect (Processor.Register (0) == 0, "x0 remains zero") && Passed;
  Passed = Expect (Processor.Pc () == 8, "second sequential PC") && Passed;
  Passed = Expect (Processor.Step ().has_value (), "signed addi executes") && Passed;
  Passed = Expect (Processor.Register (3) == 0xffffffffU, "addi uses wrapping immediate") && Passed;
  Processor.SetRegister (0, 0xffffffffU);
  Passed = Expect (Processor.Step ().has_value (), "add sourcing x0 executes") && Passed;
  Passed = Expect (Processor.Register (3) == 2, "x0 reads as zero") && Passed;
  return Passed;
}

bool TestLoadAndStore ()
{
  Memory GuestMemory (64);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x0000a183U); // lw x3, 0(x1)
  Passed = PutInstruction (GuestMemory, 4, 0x0020a223U) && Passed; // sw x2, 4(x1)
  Passed = Expect (GuestMemory.Write32 (48, 0x80000001U).has_value (), "data setup") && Passed;
  Processor.SetRegister (1, 48);
  Processor.SetRegister (2, 0xdeadbeefU);

  Passed = Expect (Processor.Step ().has_value (), "lw executes") && Passed;
  Passed = Expect (Processor.Register (3) == 0x80000001U, "lw preserves all bits") && Passed;
  Passed = Expect (Processor.Step ().has_value (), "sw executes") && Passed;
  Passed = Expect (GuestMemory.Read32 (52).value () == 0xdeadbeefU, "sw writes data") && Passed;
  return Passed;
}

bool TestBranchesAndJump ()
{
  Memory GuestMemory (64);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x00208463U); // beq x1, x2, 8
  Passed = PutInstruction (GuestMemory, 4, 0x00208463U) && Passed;
  Passed = PutInstruction (GuestMemory, 8, 0x008002efU) && Passed; // jal x5, 8
  Processor.SetRegister (1, 7);
  Processor.SetRegister (2, 7);

  Passed = Expect (Processor.Step ().has_value (), "taken beq executes") && Passed;
  Passed = Expect (Processor.Pc () == 8, "taken beq target") && Passed;
  Passed = Expect (Processor.Step ().has_value (), "jal executes") && Passed;
  Passed = Expect (Processor.Register (5) == 12, "jal link address") && Passed;
  Passed = Expect (Processor.Pc () == 16, "jal target") && Passed;

  Processor.SetPc (4);
  Processor.SetRegister (2, 8);
  Passed = Expect (Processor.Step ().has_value (), "untaken beq executes") && Passed;
  Passed = Expect (Processor.Pc () == 8, "untaken beq advances") && Passed;
  return Passed;
}

bool TestIllegalInstructions ()
{
  Memory GuestMemory (16);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0xffffffffU);
  Passed = PutInstruction (GuestMemory, 4, 0x402081b3U) && Passed; // recognized sub x3, x1, x2
  Processor.SetRegister (3, 0x12345678U);

  auto Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::IllegalInstruction, 0, "unsupported instruction traps") && Passed;
  Passed = Expect (Processor.Pc () == 0, "unsupported instruction preserves PC") && Passed;
  Passed = Expect (
    Processor.Register (3) == 0x12345678U,
    "unsupported instruction preserves registers"
    ) && Passed;

  Processor.SetPc (4);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::IllegalInstruction, 4, "unbound instruction traps") && Passed;
  Passed = Expect (Processor.Pc () == 4, "unbound instruction preserves PC") && Passed;
  Passed = Expect (
    Processor.Register (3) == 0x12345678U,
    "unbound instruction preserves registers"
    ) && Passed;
  return Passed;
}

bool TestFetchFaults ()
{
  Memory GuestMemory (16);
  Cpu Processor (GuestMemory);
  Processor.SetRegister (1, 99);
  Processor.SetPc (2);

  auto Result = Processor.Step ();
  bool Passed = ExpectTrap (
    Result, TrapCause::InstructionAddressMisaligned, 2, "misaligned fetch traps"
    );
  Passed = ExpectFaultAddress (Result, 2, "misaligned fetch address") && Passed;
  Passed = Expect (
    Processor.Register (1) == 99 && Processor.Pc () == 2,
    "fetch trap preserves state"
    ) && Passed;

  Processor.SetPc (16);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::InstructionAccessFault, 16, "fetch access traps") && Passed;
  Passed = ExpectFaultAddress (Result, 16, "fetch access address") && Passed;
  Passed = Expect (
    Processor.Register (1) == 99 && Processor.Pc () == 16,
    "access trap preserves state"
    ) && Passed;
  return Passed;
}

bool TestDataFaultsAndFailedStoreAtomicity ()
{
  Memory GuestMemory (32);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x0000a183U); // lw x3, 0(x1)
  Passed = PutInstruction (GuestMemory, 4, 0x0020a023U) && Passed; // sw x2, 0(x1)
  Passed = Expect (GuestMemory.Write32 (16, 0xaabbccddU).has_value (), "sentinel setup") && Passed;
  Processor.SetRegister (1, 18);
  Processor.SetRegister (2, 0x11223344U);
  Processor.SetRegister (3, 0x55667788U);

  auto Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::LoadAddressMisaligned, 0, "misaligned load traps") && Passed;
  Passed = ExpectFaultAddress (Result, 18, "misaligned load address") && Passed;
  Passed = Expect (Processor.Register (1) == 18, "misaligned load preserves base register") && Passed;
  Passed = Expect (Processor.Register (2) == 0x11223344U, "misaligned load preserves source register") && Passed;
  Passed = Expect (Processor.Register (3) == 0x55667788U, "misaligned load preserves destination") && Passed;
  Passed = Expect (GuestMemory.Read32 (16).value () == 0xaabbccddU,
                   "misaligned load preserves memory") && Passed;
  Passed = Expect (Processor.Pc () == 0, "misaligned load preserves PC") && Passed;

  Processor.SetPc (4);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::StoreAddressMisaligned, 4, "misaligned store traps") && Passed;
  Passed = ExpectFaultAddress (Result, 18, "misaligned store address") && Passed;
  Passed = Expect (Processor.Register (1) == 18, "misaligned store preserves base register") && Passed;
  Passed = Expect (Processor.Register (2) == 0x11223344U, "misaligned store preserves source register") && Passed;
  Passed = Expect (Processor.Register (3) == 0x55667788U, "misaligned store preserves other registers") && Passed;
  Passed = Expect (GuestMemory.Read32 (16).value () == 0xaabbccddU,
                   "misaligned store preserves memory") && Passed;
  Passed = Expect (Processor.Pc () == 4, "misaligned store preserves PC") && Passed;

  Processor.SetPc (0);
  Processor.SetRegister (1, 32);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::LoadAccessFault, 0, "load access traps") && Passed;
  Passed = ExpectFaultAddress (Result, 32, "load access address") && Passed;
  Passed = Expect (Processor.Register (1) == 32, "load access preserves base register") && Passed;
  Passed = Expect (Processor.Register (2) == 0x11223344U, "load access preserves source register") && Passed;
  Passed = Expect (Processor.Register (3) == 0x55667788U, "load access precedes writeback") && Passed;
  Passed = Expect (GuestMemory.Read32 (16).value () == 0xaabbccddU,
                   "load access preserves memory") && Passed;
  Passed = Expect (Processor.Pc () == 0, "load access preserves PC") && Passed;

  Processor.SetPc (4);
  Processor.SetRegister (1, 32);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::StoreAccessFault, 4, "store access traps") && Passed;
  Passed = ExpectFaultAddress (Result, 32, "store access address") && Passed;
  Passed = Expect (Processor.Register (1) == 32, "store access preserves base register") && Passed;
  Passed = Expect (Processor.Register (2) == 0x11223344U, "store access preserves source register") && Passed;
  Passed = Expect (Processor.Register (3) == 0x55667788U, "store access preserves other registers") && Passed;
  Passed = Expect (GuestMemory.Read32 (16).value () == 0xaabbccddU,
                   "store access preserves memory") && Passed;
  Passed = Expect (Processor.Pc () == 4, "store access preserves PC") && Passed;
  return Passed;
}

bool TestControlFlowFaultBeforeCommit ()
{
  Memory GuestMemory (32);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x00108163U); // beq x1, x1, 2
  Passed = PutInstruction (GuestMemory, 4, 0x002002efU) && Passed; // jal x5, 2
  Passed = PutInstruction (GuestMemory, 8, 0x00208163U) && Passed; // beq x1, x2, 2
  Processor.SetRegister (1, 1);
  Processor.SetRegister (5, 0xabcdef01U);

  auto Result = Processor.Step ();
  Passed = ExpectTrap (
    Result, TrapCause::InstructionAddressMisaligned, 0, "taken misaligned branch traps"
    ) && Passed;
  Passed = ExpectFaultAddress (Result, 2, "branch target address") && Passed;
  Passed = Expect (Processor.Pc () == 0, "branch trap preserves PC") && Passed;

  Processor.SetPc (4);
  Result = Processor.Step ();
  Passed = ExpectTrap (Result, TrapCause::InstructionAddressMisaligned, 4, "misaligned jal traps") && Passed;
  Passed = ExpectFaultAddress (Result, 6, "jal target address") && Passed;
  Passed = Expect (Processor.Register (5) == 0xabcdef01U, "jal trap precedes link write") && Passed;
  Passed = Expect (Processor.Pc () == 4, "jal trap preserves PC") && Passed;

  Processor.SetPc (8);
  Processor.SetRegister (2, 2);
  Passed = Expect (Processor.Step ().has_value (), "untaken misaligned branch target is ignored") && Passed;
  Passed = Expect (Processor.Pc () == 12, "untaken branch advances sequentially") && Passed;
  return Passed;
}

bool TestAddressAndControlFlowWrapping ()
{
  Memory GuestMemory (32);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x0100a183U); // lw x3, 16(x1)
  Passed = PutInstruction (GuestMemory, 4, 0xffdff2efU) && Passed; // jal x5, -4
  Passed = Expect (GuestMemory.Write32 (8, 0x12345678U).has_value (), "wrapped data setup") && Passed;
  Processor.SetRegister (1, 0xfffffff8U);

  Passed = Expect (Processor.Step ().has_value (), "wrapped effective address executes") && Passed;
  Passed = Expect (Processor.Register (3) == 0x12345678U, "effective address wraps in RV32") && Passed;
  Passed = Expect (Processor.Step ().has_value (), "wrapped jal target executes") && Passed;
  Passed = Expect (Processor.Register (5) == 8, "wrapped jal link is sequential PC") && Passed;
  Passed = Expect (Processor.Pc () == 0, "jal target wraps in RV32") && Passed;
  return Passed;
}

bool TestAlignedJumpFaultsOnNextFetch ()
{
  Memory GuestMemory (16);
  Cpu Processor (GuestMemory);
  bool Passed = PutInstruction (GuestMemory, 0, 0x010002efU); // jal x5, 16

  Passed = Expect (Processor.Step ().has_value (), "aligned out-of-range jal commits") && Passed;
  Passed = Expect (Processor.Register (5) == 4 && Processor.Pc () == 16, "jal effects commit") && Passed;
  Passed = ExpectTrap (
    Processor.Step (), TrapCause::InstructionAccessFault, 16, "next fetch traps"
    ) && Passed;
  return Passed;
}

bool TestHostRegisterIndexValidation ()
{
  Memory GuestMemory (4);
  Cpu Processor (GuestMemory);
  bool ReadRejected = false;
  bool WriteRejected = false;

  try {
    static_cast<void> (Processor.Register (32));
  } catch (const std::out_of_range &) {
    ReadRejected = true;
  }
  try {
    Processor.SetRegister (32, 1);
  } catch (const std::out_of_range &) {
    WriteRejected = true;
  }

  return Expect (ReadRejected, "invalid host register read is rejected") &&
         Expect (WriteRejected, "invalid host register write is rejected");
}

} // namespace

int main ()
{
  bool Passed = TestMemoryAccess ();
  Passed = TestArithmeticX0AndAliasing () && Passed;
  Passed = TestLoadAndStore () && Passed;
  Passed = TestBranchesAndJump () && Passed;
  Passed = TestIllegalInstructions () && Passed;
  Passed = TestFetchFaults () && Passed;
  Passed = TestDataFaultsAndFailedStoreAtomicity () && Passed;
  Passed = TestControlFlowFaultBeforeCommit () && Passed;
  Passed = TestAlignedJumpFaultsOnNextFetch () && Passed;
  Passed = TestAddressAndControlFlowWrapping () && Passed;
  Passed = TestHostRegisterIndexValidation () && Passed;
  return Passed ? 0 : 1;
}
