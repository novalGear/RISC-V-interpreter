#pragma once

#include <cstdint>

namespace riscv {

class RegIndex {
public:
    static constexpr RegIndex from_encoded(std::uint8_t bits) {
        return RegIndex{bits};
    }

    constexpr std::uint8_t value() const { return value_; }

private:
    explicit constexpr RegIndex(std::uint8_t value) : value_(value) {}

    std::uint8_t value_;
};

} // namespace riscv
