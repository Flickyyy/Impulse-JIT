#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace impulse::ir {

struct Module;
struct Function;
struct Binding;
struct Struct;
struct StructField;
struct BasicBlock;
struct Instruction;

enum class InstructionKind : std::uint8_t {
    Comment,
    Return,
    Literal,
    Reference,
    Binary,
    Unary,
    Store,
    Branch,
    BranchIf,
    Label,
    Call,
};

struct Instruction {
    InstructionKind kind = InstructionKind::Comment;
    std::vector<std::string> operands;
};

struct BasicBlock {
    std::string label;
    std::vector<Instruction> instructions;
};

enum class StorageClass : std::uint8_t {
    Let,
    Const,
    Var,
};

struct Binding {
    StorageClass storage;
    std::string name;
    std::string type;
    std::string initializer;
    std::optional<std::string> constant_value;
    bool exported = false;
    std::vector<Instruction> initializer_instructions;
};

struct FunctionParameter {
    std::string name;
    std::string type;
};

struct Function {
    std::string name;
    std::vector<FunctionParameter> parameters;
    std::optional<std::string> return_type;
    std::string body_snippet;
    std::vector<BasicBlock> blocks;
    bool exported = false;
};

struct InterfaceMethod {
    std::string name;
    std::vector<FunctionParameter> parameters;
    std::optional<std::string> return_type;
};

struct Interface {
    std::string name;
    std::vector<InterfaceMethod> methods;
    bool exported = false;
};

struct StructField {
    std::string name;
    std::string type;
};

struct Struct {
    std::string name;
    std::vector<StructField> fields;
    bool exported = false;
};

struct Module {
    std::vector<std::string> path;
    std::vector<Binding> bindings;
    std::vector<Function> functions;
    std::vector<Struct> structs;
    std::vector<Interface> interfaces;
};

}  // namespace impulse::ir
