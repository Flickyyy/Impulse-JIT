#pragma once

#include <iosfwd>

#include "impulse/ir/cfg.h"
#include "impulse/ir/ir.h"
#include "impulse/ir/ssa.h"

namespace impulse::ir {

void dump_ir(const Module& module, std::ostream& out);
void dump_cfg(const ControlFlowGraph& cfg, std::ostream& out);
void dump_ssa(const SsaFunction& function, std::ostream& out, bool include_metadata = true);

}  // namespace impulse::ir
