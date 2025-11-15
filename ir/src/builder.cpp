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
        function_->blocks.push_back(BasicBlock{.label = "entry"});
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
    function_->blocks.push_back(BasicBlock{.label = std::move(label)});
    currentIndex_ = function_->blocks.size() - 1;
    return function_->blocks.back();
}

auto FunctionBuilder::append(InstructionKind kind, std::vector<std::string> operands) -> Instruction& {
    auto& block = current();
    block.instructions.push_back(Instruction{
        .kind = kind,
        .operands = std::move(operands),
    });
    return block.instructions.back();
}

auto FunctionBuilder::appendComment(std::string text) -> Instruction& {
    return append(InstructionKind::Comment, std::vector<std::string>{std::move(text)});
}

}  // namespace impulse::ir
