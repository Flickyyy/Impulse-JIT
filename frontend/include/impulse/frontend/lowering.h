#pragma once

#include <string>

#include "impulse/frontend/ast.h"
#include "impulse/ir/ir.h"

namespace impulse::frontend {

[[nodiscard]] auto lower_to_ir(const Module& module) -> ir::Module;
[[nodiscard]] auto emit_ir_text(const Module& module) -> std::string;

}  // namespace impulse::frontend
