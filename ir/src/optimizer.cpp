#include "impulse/ir/optimizer.h"

namespace impulse::ir {

auto optimize_ssa(SsaFunction& /*function*/) -> bool {
    // Optimizations removed - JIT handles performance
    return false;
}

}  // namespace impulse::ir
