#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "impulse/ir/ssa.h"

namespace impulse::jit {

// JIT compiled function signature: takes array of doubles, returns double
using JitFunction = double (*)(double*);

// Memory region for executable code
class CodeBuffer {
public:
    CodeBuffer();
    ~CodeBuffer();

    CodeBuffer(const CodeBuffer&) = delete;
    auto operator=(const CodeBuffer&) -> CodeBuffer& = delete;
    
    CodeBuffer(CodeBuffer&& other) noexcept;
    auto operator=(CodeBuffer&& other) noexcept -> CodeBuffer&;

    // Emit raw bytes
    void emit(uint8_t byte);
    void emit(const std::vector<uint8_t>& bytes);
    
    // Emit common x86-64 instructions
    void emit_push_rbp();
    void emit_pop_rbp();
    void emit_mov_rbp_rsp();
    void emit_mov_rsp_rbp();
    void emit_ret();
    
    // Floating-point operations (SSE/AVX)
    void emit_movsd_xmm_mem(int xmm, int base_reg, int32_t offset);
    void emit_movsd_mem_xmm(int base_reg, int32_t offset, int xmm);
    void emit_movsd_xmm_xmm(int dst, int src);
    
    void emit_addsd(int dst, int src);
    void emit_subsd(int dst, int src);
    void emit_mulsd(int dst, int src);
    void emit_divsd(int dst, int src);
    
    // Comparison
    void emit_ucomisd(int xmm1, int xmm2);
    void emit_seta(int reg8);   // unsigned above (>)
    void emit_setae(int reg8);  // unsigned above or equal (>=)
    void emit_setb(int reg8);   // unsigned below (<)
    void emit_setbe(int reg8);  // unsigned below or equal (<=)
    void emit_sete(int reg8);   // equal (==)
    void emit_setne(int reg8);  // not equal (!=)
    
    // Integer operations
    void emit_mov_reg_imm64(int reg, int64_t imm);
    void emit_mov_reg_mem(int reg, int base_reg, int32_t offset);
    void emit_mov_mem_reg(int base_reg, int32_t offset, int reg);
    void emit_xor_reg_reg(int dst, int src);
    
    // Control flow
    void emit_jmp_rel32(int32_t offset);
    void emit_jne_rel32(int32_t offset);
    void emit_je_rel32(int32_t offset);
    void emit_test_reg_reg(int reg1, int reg2);
    
    // Get current position for patching
    [[nodiscard]] auto position() const -> size_t;
    
    // Patch a relative offset at a given position
    void patch_rel32(size_t pos, int32_t offset);
    
    // Finalize and make executable
    [[nodiscard]] auto finalize() -> JitFunction;

private:
    std::vector<uint8_t> code_;
    void* executable_ = nullptr;
    size_t executable_size_ = 0;
};

// Register allocation
enum class Register : int {
    RAX = 0, RCX = 1, RDX = 2, RBX = 3,
    RSP = 4, RBP = 5, RSI = 6, RDI = 7,
    R8 = 8, R9 = 9, R10 = 10, R11 = 11,
    R12 = 12, R13 = 13, R14 = 14, R15 = 15
};

enum class XmmRegister : int {
    XMM0 = 0, XMM1 = 1, XMM2 = 2, XMM3 = 3,
    XMM4 = 4, XMM5 = 5, XMM6 = 6, XMM7 = 7,
    XMM8 = 8, XMM9 = 9, XMM10 = 10, XMM11 = 11,
    XMM12 = 12, XMM13 = 13, XMM14 = 14, XMM15 = 15
};

// JIT Compiler for SSA functions
class JitCompiler {
public:
    JitCompiler();
    
    // Compile an SSA function to native code
    [[nodiscard]] auto compile(const ir::SsaFunction& function) -> JitFunction;
    
    // Check if JIT is supported on this platform
    [[nodiscard]] static auto is_supported() -> bool;

private:
    CodeBuffer buffer_;
    
    // SSA value to stack offset mapping
    std::unordered_map<uint64_t, int32_t> value_offsets_;
    int32_t stack_size_ = 0;
    
    // Label positions for patching
    std::unordered_map<std::string, size_t> label_positions_;
    std::vector<std::pair<size_t, std::string>> pending_jumps_;
    
    void compile_block(const ir::SsaBlock& block);
    void compile_instruction(const ir::SsaInstruction& inst);
    
    void emit_prologue(int num_locals);
    void emit_epilogue();
    
    [[nodiscard]] auto get_value_offset(const ir::SsaValue& value) -> int32_t;
    void allocate_value(const ir::SsaValue& value);
    
    void load_value_to_xmm(int xmm, const ir::SsaValue& value);
    void store_xmm_to_value(const ir::SsaValue& value, int xmm);
};

}  // namespace impulse::jit
