#include "impulse/ir/optimizer.h"

#include "impulse/ir/constant_propagation.h"
#include "impulse/ir/copy_propagation.h"
#include "impulse/ir/dead_code_elimination.h"

namespace impulse::ir {

auto optimize_ssa(SsaFunction& function) -> bool {
    bool mutated = false;
    bool changed = true;

    while (changed) {
        changed = false;

        if (propagate_constants(function)) {
            changed = true;
            mutated = true;
        }

        if (propagate_copies(function)) {
            changed = true;
            mutated = true;
        }

        if (eliminate_dead_assignments(function)) {
            changed = true;
            mutated = true;
        }
    }

    return mutated;
}

}  // namespace impulse::ir
