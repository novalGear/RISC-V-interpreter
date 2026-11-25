#include "cpu/execution_binding.hpp"

#include "decoder/decode_table.hpp"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace riscv::cpu::internal {
namespace {

using decoder::Interpretation;

struct OperandContract {
  std::string_view Name;
  Interpretation InterpretationKind;
  unsigned Width;
};

struct BindingContract {
  std::string_view Name;
  ExecutionKind Kind;
  std::span<const OperandContract> Operands;
};

constexpr std::array RegisterRegisterOperands{
  OperandContract{"rd", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"rs1", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"rs2", Interpretation::IntegerRegisterIndex, 5},
};
constexpr std::array RegisterImmediateOperands{
  OperandContract{"rd", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"rs1", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"imm", Interpretation::Signed, 12},
};
constexpr std::array StoreOperands{
  OperandContract{"rs1", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"rs2", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"imm", Interpretation::Signed, 12},
};
constexpr std::array BranchOperands{
  OperandContract{"rs1", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"rs2", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"imm", Interpretation::Signed, 13},
};
constexpr std::array JumpOperands{
  OperandContract{"rd", Interpretation::IntegerRegisterIndex, 5},
  OperandContract{"imm", Interpretation::Signed, 21},
};
constexpr std::array Contracts{
  BindingContract{"add", ExecutionKind::Add, RegisterRegisterOperands},
  BindingContract{"addi", ExecutionKind::Addi, RegisterImmediateOperands},
  BindingContract{"lw", ExecutionKind::Lw, RegisterImmediateOperands},
  BindingContract{"sw", ExecutionKind::Sw, StoreOperands},
  BindingContract{"beq", ExecutionKind::Beq, BranchOperands},
  BindingContract{"jal", ExecutionKind::Jal, JumpOperands},
};

void ValidateOperands (
  const decoder::InstructionDescriptor &Descriptor,
  const BindingContract &Contract
  )
{
  if (Descriptor.operands.size () != Contract.Operands.size ()) {
    throw std::logic_error ("execution binding operand count mismatch");
  }

  for (std::size_t Index = 0; Index < Contract.Operands.size (); ++Index) {
    const decoder::OperandDescriptor &Operand = Descriptor.operands[Index];
    const OperandContract &Expected = Contract.Operands[Index];

    if (Operand.name != Expected.Name ||
        Operand.interpretation != Expected.InterpretationKind ||
        Operand.width != Expected.Width) {
      throw std::logic_error ("execution binding operand contract mismatch");
    }
  }
}

std::vector<ExecutionBinding> BuildBindings ()
{
  std::span<const decoder::InstructionDescriptor> Descriptors = decoder::InstructionTable ();
  std::vector<ExecutionBinding> Bindings (Descriptors.size (), {ExecutionKind::Unbound});
  std::array<bool, Contracts.size ()> Found{};

  for (std::size_t DescriptorIndex = 0; DescriptorIndex < Descriptors.size (); ++DescriptorIndex) {
    const decoder::InstructionDescriptor &Descriptor = Descriptors[DescriptorIndex];

    for (std::size_t ContractIndex = 0; ContractIndex < Contracts.size (); ++ContractIndex) {
      const BindingContract &Contract = Contracts[ContractIndex];

      if (Descriptor.name == Contract.Name) {
        if (Found[ContractIndex]) {
          throw std::logic_error ("duplicate descriptor for execution binding");
        }
        ValidateOperands (Descriptor, Contract);
        Bindings[DescriptorIndex] = ExecutionBinding{Contract.Kind};
        Found[ContractIndex] = true;
        break;
      }
    }
  }

  for (bool WasFound : Found) {
    if (!WasFound) {
      throw std::logic_error ("missing descriptor for execution binding");
    }
  }

  return Bindings;
}

const std::vector<ExecutionBinding> &Bindings ()
{
  static const std::vector<ExecutionBinding> Table = BuildBindings ();
  return Table;
}

} // namespace

const ExecutionBinding &ExecutionBindingFor (decoder::InstructionId Id)
{
  return Bindings ()[Id.Ordinal_];
}

} // namespace riscv::cpu::internal
