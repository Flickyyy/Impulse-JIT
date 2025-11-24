#pragma once

#include "impulse/ir/ssa.h"

namespace impulse::ir {

[[nodiscard]] auto optimize_ssa(SsaFunction& function) -> bool;

}  // namespace impulse::ir
