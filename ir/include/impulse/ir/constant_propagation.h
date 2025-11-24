#pragma once

#include "impulse/ir/ssa.h"

namespace impulse::ir {

[[nodiscard]] auto propagate_constants(SsaFunction& function) -> bool;

}  // namespace impulse::ir
