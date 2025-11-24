#pragma once

#include "impulse/ir/ssa.h"

namespace impulse::ir {

[[nodiscard]] auto eliminate_dead_assignments(SsaFunction& function) -> bool;

}  // namespace impulse::ir
