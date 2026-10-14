#pragma once

#include <span>
#include <string_view>

// A non-owning view of names generated for one ISA configuration.
// The backing arrays and strings must outlive this value.
struct IsaNames {
    std::span<const std::string_view> instruction_names;
    std::span<const std::string_view> register_names;
};
