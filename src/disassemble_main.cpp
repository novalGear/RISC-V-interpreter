#include "disassembler/disassembler.hpp"
#include "executable/elf_code_sections.hpp"

#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string_view>

int main (int ArgumentCount, char **Arguments)
{
  bool Strict = ArgumentCount == 3 && std::string_view (Arguments[1]) == "--strict";
  bool HasPath = (ArgumentCount == 2 &&
                  std::string_view (Arguments[1]) != "--strict") ||
                 (Strict && std::string_view (Arguments[2]) != "--strict");
  if (!HasPath) {
    std::cerr << "usage: riscv-disassemble [--strict] <elf-file>\n";
    return 2;
  }

  std::filesystem::path Path (Arguments[Strict ? 2 : 1]);
  auto Sections = riscv::executable::LoadElfCodeSections (Path);
  if (!Sections) {
    std::cerr << Path << ": " << Sections.error ().message << '\n';
    return 1;
  }

  bool UnsupportedInstruction = false;
  for (const riscv::executable::CodeSection &Section : *Sections) {
    if (Section.address () % 4 != 0) {
      std::cerr << Path << ": executable section " << Section.name ()
                << " at 0x" << std::hex << Section.address () << std::dec
                << " is not aligned for 32-bit instructions\n";
      return 1;
    }

    std::span<const std::byte> Bytes = Section.bytes ();
    for (const riscv::disassembler::DisassemblyRecord &Record :
         riscv::disassembler::Disassemble (Bytes, Section.address ())) {
      std::cout << std::hex << std::setfill ('0') << std::setw (8)
                << Record.address << ": " << std::setw (8) << Record.word
                << "  " << Record.text << '\n';

      if (Strict && Record.kind == riscv::disassembler::RecordKind::UnknownWord) {
        std::cerr << Path << ": unsupported instruction at 0x"
                  << std::hex << std::setfill ('0') << std::setw (8)
                  << Record.address << ": raw 0x" << std::setw (8)
                  << Record.word << std::dec << '\n';
        UnsupportedInstruction = true;
      }
    }

    if (Bytes.size () % 4 != 0) {
      std::cerr << Path << ": ignored " << Bytes.size () % 4
                << " trailing byte(s) in executable section " << Section.name ()
                << '\n';
    }
  }

  return UnsupportedInstruction ? 1 : 0;
}
