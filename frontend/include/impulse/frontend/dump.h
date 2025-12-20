#pragma once

#include <iosfwd>
#include <vector>

#include "impulse/frontend/ast.h"
#include "impulse/frontend/lexer.h"

namespace impulse::frontend {

void dump_tokens(const std::vector<Token>& tokens, std::ostream& out);
void dump_ast(const Module& module, std::ostream& out);

}  // namespace impulse::frontend
