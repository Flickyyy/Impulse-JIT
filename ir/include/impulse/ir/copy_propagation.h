#pragma once

#include "impulse/ir/ssa.h"

namespace impulse::ir {

[[nodiscard]] auto propagate_copies(SsaFunction& function) -> bool;

}  // namespace impulse::ir
