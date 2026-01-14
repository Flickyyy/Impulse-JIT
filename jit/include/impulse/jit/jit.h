#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
    // parameter_names: names of parameters in order (to map to args array indices)
    [[nodiscard]] auto compile(const ir::SsaFunction& function, const std::vector<std::string>& parameter_names) -> JitFunction;
    
    // Compile and return both the function and the code buffer (for caching)
    // parameter_names: names of parameters in order (to map to args array indices)
    [[nodiscard]] auto compile_with_buffer(const ir::SsaFunction& function, const std::vector<std::string>& parameter_names) -> std::pair<JitFunction, CodeBuffer>;
    
    // Check if JIT is supported on this platform
    [[nodiscard]] static auto is_supported() -> bool;
    
    // JIT optimization statistics
    struct JitOptStats {
        int constants_folded = 0;      // Binary ops on constants computed at JIT time
        int strength_reductions = 0;   // x*2 → x+x, etc.
        int identity_eliminations = 0; // x*1 → x, x+0 → x, etc.
        int dead_code_eliminated = 0;  // Instructions with unused results skipped
        int loops_unrolled = 0;       // Count of loops unrolled by JIT
    };
    
    [[nodiscard]] auto get_opt_stats() const -> const JitOptStats& { return opt_stats_; }

private:
    CodeBuffer buffer_;
    
    // SSA value to stack offset mapping
    std::unordered_map<uint64_t, int32_t> value_offsets_;
    int32_t stack_size_ = 0;
    
    // Label positions for patching
    std::unordered_map<std::string, size_t> label_positions_;
    std::vector<std::pair<size_t, std::string>> pending_jumps_;
    
    // Phi node map: target block name -> list of (predecessor block id, phi result, phi input value)
    struct PhiInfo {
        std::size_t pred_block_id;
        ir::SsaValue result;
        ir::SsaValue input;
    };
    std::unordered_map<std::string, std::vector<PhiInfo>> phi_map_;
    std::size_t current_block_id_ = 0;
    
    // Dead Code Elimination: set of live (used) SSA values
    std::unordered_set<uint64_t> live_values_;
    
    // JIT-time constant tracking for optimization
    std::unordered_map<uint64_t, double> known_constants_;
    JitOptStats opt_stats_;

    // Loop unrolling bookkeeping: header -> (body, trip_count)
    std::unordered_map<std::string, std::pair<std::string, int>> unrolled_loops_;
    // Blocks that were emitted as part of unrolling and should be skipped
    std::unordered_set<std::string> skip_blocks_;
    
    // Helper to get SSA value key
    [[nodiscard]] static auto value_key(const ir::SsaValue& v) -> uint64_t {
        return (static_cast<uint64_t>(v.symbol) << 32) | v.version;
    }
    
    // Check if a value is a known constant
    [[nodiscard]] auto is_constant(const ir::SsaValue& v) const -> bool {
        return known_constants_.find(value_key(v)) != known_constants_.end();
    }
    
    // Get constant value (assumes is_constant returned true)
    [[nodiscard]] auto get_constant(const ir::SsaValue& v) const -> double {
        return known_constants_.at(value_key(v));
    }
    
    // Store a known constant
    void set_constant(const ir::SsaValue& v, double val) {
        known_constants_[value_key(v)] = val;
    }
    
    void compile_block(const ir::SsaBlock& block, const ir::SsaFunction& function);
    void compile_instruction(const ir::SsaInstruction& inst, const ir::SsaBlock& block, const ir::SsaFunction& function);
    
    // Dead Code Elimination: compute set of live values
    void compute_live_values(const ir::SsaFunction& function);
    
    void emit_prologue(int num_locals);
    void emit_epilogue();
    
    // Emit phi moves for a jump to target block from current block
    void emit_phi_moves(const std::string& target_block);
    
    [[nodiscard]] auto get_value_offset(const ir::SsaValue& value) -> int32_t;
    void allocate_value(const ir::SsaValue& value);
    
    void load_value_to_xmm(int xmm, const ir::SsaValue& value);
    void store_xmm_to_value(const ir::SsaValue& value, int xmm);
    
    // Emit a constant load (used for JIT-time computed values)
    void emit_constant_to_result(double val, const ir::SsaValue& result);
};

}  // namespace impulse::jit
