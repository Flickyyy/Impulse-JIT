#pragma once

#include <string>

#include "impulse/ir/ir.h"

namespace impulse::ir {

[[nodiscard]] auto print_module(const Module& module) -> std::string;

}  // namespace impulse::ir
