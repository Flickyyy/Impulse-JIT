#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "impulse/ir/ir.h"

namespace impulse::ir {

class FunctionBuilder {
   public:
    explicit FunctionBuilder(Function& function);

    [[nodiscard]] auto entry() -> BasicBlock&;
    [[nodiscard]] auto current() -> BasicBlock&;

    auto newBlock(std::string label) -> BasicBlock&;
    auto append(InstructionKind kind, std::vector<std::string> operands = {}) -> Instruction&;
    auto appendComment(std::string text) -> Instruction&;

   private:
    Function* function_ = nullptr;
    std::size_t currentIndex_ = 0;

    void ensureEntry();
 };

}  // namespace impulse::ir
