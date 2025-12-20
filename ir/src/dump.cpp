#include "impulse/ir/dump.h"

#include <limits>
#include <ostream>
#include <string>

#include "impulse/ir/printer.h"

namespace impulse::ir {
namespace {

void indent(std::ostream& out, std::size_t depth) {
    for (std::size_t i = 0; i < depth; ++i) {
        out << "  ";
    }
}

[[nodiscard]] auto escape_string(const std::string& value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    for (char ch : value) {
        switch (ch) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\t':
                escaped += "\\t";
                break;
            case '\r':
                escaped += "\\r";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

auto instruction_to_string(const Instruction& inst) -> std::string {
    switch (inst.kind) {
        case InstructionKind::Comment:
            if (!inst.operands.empty()) {
                return "# " + inst.operands.front();
            }
            return "# <comment>";
        case InstructionKind::Return:
            if (!inst.operands.empty() && !inst.operands.front().empty()) {
                return "return " + inst.operands.front();
            }
            return "return";
        case InstructionKind::Literal:
            if (!inst.operands.empty()) {
                return "literal " + inst.operands.front();
            }
            return "literal";
        case InstructionKind::StringLiteral:
            if (!inst.operands.empty()) {
                return "literal_string \"" + escape_string(inst.operands.front()) + "\"";
            }
            return "literal_string";
        case InstructionKind::Reference:
            if (!inst.operands.empty()) {
                return "reference " + inst.operands.front();
            }
            return "reference";
        case InstructionKind::Binary:
            if (!inst.operands.empty()) {
                return "binary " + inst.operands.front();
            }
            return "binary";
        case InstructionKind::Unary:
            if (!inst.operands.empty()) {
                return "unary " + inst.operands.front();
            }
            return "unary";
        case InstructionKind::Store:
            if (!inst.operands.empty()) {
                return "store " + inst.operands.front();
            }
            return "store";
        case InstructionKind::Drop:
            return "drop";
        case InstructionKind::Branch:
            if (!inst.operands.empty()) {
                return "branch " + inst.operands.front();
            }
            return "branch";
        case InstructionKind::BranchIf:
            if (inst.operands.size() >= 2) {
                return "branch_if " + inst.operands[0] + " when " + inst.operands[1];
            }
            return "branch_if";
        case InstructionKind::Label:
            if (inst.operands.empty()) {
                return "label:";
            }
            return inst.operands.front() + ':';
        case InstructionKind::Call: {
            std::string result = "call";
            if (!inst.operands.empty()) {
                result += ' ' + inst.operands.front();
                if (inst.operands.size() > 1) {
                    result += " (" + inst.operands[1] + " args)";
                }
            }
            return result;
        }
        case InstructionKind::MakeArray:
            return "make_array";
        case InstructionKind::ArrayGet:
            return "array_get";
        case InstructionKind::ArraySet:
            return "array_set";
        case InstructionKind::ArrayLength:
            return "array_length";
    }
    return "<instruction>";
}

void dump_instruction(const Instruction& inst, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << instruction_to_string(inst) << '\n';
}

void dump_block(const ControlFlowGraph::Block& block, const ControlFlowGraph& cfg, std::ostream& out,
                std::size_t depth) {
    indent(out, depth);
    out << "Block " << block.name << " (#" << block.start_index << ".." << block.end_index << ")\n";

    if (!block.successors.empty()) {
        indent(out, depth + 1);
        out << "Successors";
        for (auto succ : block.successors) {
            out << ' ' << succ;
        }
        out << '\n';
    }

    if (!block.predecessors.empty()) {
        indent(out, depth + 1);
        out << "Predecessors";
        for (auto pred : block.predecessors) {
            out << ' ' << pred;
        }
        out << '\n';
    }

    indent(out, depth + 1);
    out << "Instructions" << '\n';
    for (std::size_t i = block.start_index; i < block.end_index && i < cfg.instructions.size(); ++i) {
        if (cfg.instructions[i] != nullptr) {
            dump_instruction(*cfg.instructions[i], out, depth + 2);
        }
    }
}

void dump_phi(const PhiNode& phi, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    out << phi.result.to_string() << " = phi";
    if (phi.inputs.empty()) {
        out << " ()\n";
        return;
    }
    out << '\n';
    for (const auto& input : phi.inputs) {
        indent(out, depth + 1);
        out << "from block " << input.predecessor << ' ';
        if (input.value.has_value()) {
            out << input.value->to_string();
        } else {
            out << "<undefined>";
        }
        out << '\n';
    }
}

void dump_ssa_instruction(const SsaInstruction& inst, std::ostream& out, std::size_t depth) {
    indent(out, depth);
    if (inst.result.has_value()) {
        out << inst.result->to_string() << " = ";
    }
    out << inst.opcode;
    if (!inst.arguments.empty()) {
        out << ' ';
        for (std::size_t i = 0; i < inst.arguments.size(); ++i) {
            if (i != 0) {
                out << ' ';
            }
            out << inst.arguments[i].to_string();
        }
    }
    if (!inst.immediates.empty()) {
        out << " |";
        for (const auto& imm : inst.immediates) {
            out << ' ' << imm;
        }
    }
    out << '\n';
}

void dump_ssa_block(const SsaBlock& block, std::ostream& out, bool include_metadata, std::size_t depth) {
    indent(out, depth);
    out << "Block #" << block.id << " (" << block.name << ")";
    if (include_metadata) {
        out << " idom=";
        if (block.immediate_dominator == std::numeric_limits<std::size_t>::max()) {
            out << "<none>";
        } else {
            out << block.immediate_dominator;
        }
    }
    out << '\n';

    if (!block.phi_nodes.empty()) {
        indent(out, depth + 1);
        out << "Phi" << '\n';
        for (const auto& phi : block.phi_nodes) {
            dump_phi(phi, out, depth + 2);
        }
    }

    indent(out, depth + 1);
    out << "Instructions" << '\n';
    for (const auto& inst : block.instructions) {
        dump_ssa_instruction(inst, out, depth + 2);
    }

    if (include_metadata) {
        if (!block.successors.empty()) {
            indent(out, depth + 1);
            out << "Successors";
            for (auto succ : block.successors) {
                out << ' ' << succ;
            }
            out << '\n';
        }
        if (!block.predecessors.empty()) {
            indent(out, depth + 1);
            out << "Predecessors";
            for (auto pred : block.predecessors) {
                out << ' ' << pred;
            }
            out << '\n';
        }
        if (!block.dominator_children.empty()) {
            indent(out, depth + 1);
            out << "DomChildren";
            for (auto child : block.dominator_children) {
                out << ' ' << child;
            }
            out << '\n';
        }
        if (!block.dominance_frontier.empty()) {
            indent(out, depth + 1);
            out << "DomFrontier";
            for (auto f : block.dominance_frontier) {
                out << ' ' << f;
            }
            out << '\n';
        }
    }
}

}  // namespace

void dump_ir(const Module& module, std::ostream& out) {
    out << print_module(module);
}

void dump_cfg(const ControlFlowGraph& cfg, std::ostream& out) {
    out << "CFG" << '\n';
    for (std::size_t i = 0; i < cfg.blocks.size(); ++i) {
        dump_block(cfg.blocks[i], cfg, out, 1);
    }
}

void dump_ssa(const SsaFunction& function, std::ostream& out, bool include_metadata) {
    out << "Function " << function.name << '\n';
    for (const auto& block : function.blocks) {
        dump_ssa_block(block, out, include_metadata, 1);
    }
}

}  // namespace impulse::ir
