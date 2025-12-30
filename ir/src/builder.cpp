#include "impulse/ir/builder.h"

#include <stdexcept>
#include <utility>

namespace impulse::ir {

FunctionBuilder::FunctionBuilder(Function& function) : function_(&function) {
    ensureEntry();
}

void FunctionBuilder::ensureEntry() {
    if (function_ == nullptr) {
        return;
    }
    if (function_->blocks.empty()) {
        BasicBlock entry_block;
        entry_block.label = "entry";
        function_->blocks.push_back(std::move(entry_block));
        currentIndex_ = 0;
    } else if (currentIndex_ >= function_->blocks.size()) {
        currentIndex_ = 0;
    }
}

auto FunctionBuilder::entry() -> BasicBlock& {
    ensureEntry();
    if (function_ == nullptr) {
        throw std::runtime_error("FunctionBuilder not initialized");
    }
    return function_->blocks.front();
}

auto FunctionBuilder::current() -> BasicBlock& {
    ensureEntry();
    if (function_ == nullptr) {
        throw std::runtime_error("FunctionBuilder not initialized");
    }
    return function_->blocks[currentIndex_];
}

auto FunctionBuilder::newBlock(std::string label) -> BasicBlock& {
    if (function_ == nullptr) {
        throw std::runtime_error("FunctionBuilder not initialized");
    }
    if (label.empty()) {
        label = "block" + std::to_string(function_->blocks.size());
    }
    BasicBlock new_block;
    new_block.label = std::move(label);
    function_->blocks.push_back(std::move(new_block));
    currentIndex_ = function_->blocks.size() - 1;
    return function_->blocks.back();
}

auto FunctionBuilder::append(InstructionKind kind, std::vector<std::string> operands) -> Instruction& {
    auto& block = current();
    Instruction inst;
    inst.kind = kind;
    inst.operands = std::move(operands);
    block.instructions.push_back(std::move(inst));
    return block.instructions.back();
}

auto FunctionBuilder::appendComment(std::string text) -> Instruction& {
    return append(InstructionKind::Comment, std::vector<std::string>{std::move(text)});
}

}  // namespace impulse::ir
