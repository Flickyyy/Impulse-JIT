#include "impulse/jit/jit.h"

#include <cstring>
#include <stdexcept>

#ifdef __linux__
#include <sys/mman.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

namespace impulse::jit {

// ============================================================================
// CodeBuffer implementation
// ============================================================================

CodeBuffer::CodeBuffer() {
    code_.reserve(4096);
}

CodeBuffer::~CodeBuffer() {
    if (executable_ != nullptr) {
#ifdef __linux__
        munmap(executable_, executable_size_);
#endif
#ifdef _WIN32
        VirtualFree(executable_, 0, MEM_RELEASE);
#endif
    }
}

CodeBuffer::CodeBuffer(CodeBuffer&& other) noexcept
    : code_(std::move(other.code_)),
      executable_(other.executable_),
      executable_size_(other.executable_size_) {
    other.executable_ = nullptr;
    other.executable_size_ = 0;
}

auto CodeBuffer::operator=(CodeBuffer&& other) noexcept -> CodeBuffer& {
    if (this != &other) {
        if (executable_ != nullptr) {
#ifdef __linux__
            munmap(executable_, executable_size_);
#endif
#ifdef _WIN32
            VirtualFree(executable_, 0, MEM_RELEASE);
#endif
        }
        code_ = std::move(other.code_);
        executable_ = other.executable_;
        executable_size_ = other.executable_size_;
        other.executable_ = nullptr;
        other.executable_size_ = 0;
    }
    return *this;
}

void CodeBuffer::emit(uint8_t byte) {
    code_.push_back(byte);
}

void CodeBuffer::emit(const std::vector<uint8_t>& bytes) {
    code_.insert(code_.end(), bytes.begin(), bytes.end());
}

void CodeBuffer::emit_push_rbp() {
    emit(0x55);  // push rbp
}

void CodeBuffer::emit_pop_rbp() {
    emit(0x5D);  // pop rbp
}

void CodeBuffer::emit_mov_rbp_rsp() {
    emit({0x48, 0x89, 0xE5});  // mov rbp, rsp
}

void CodeBuffer::emit_mov_rsp_rbp() {
    emit({0x48, 0x89, 0xEC});  // mov rsp, rbp
}

void CodeBuffer::emit_ret() {
    emit(0xC3);  // ret
}

void CodeBuffer::emit_movsd_xmm_mem(int xmm, int base_reg, int32_t offset) {
    // movsd xmm, [base_reg + offset]
    // F2 0F 10 /r or F2 REX.R 0F 10 /r for xmm8-15 or r8-15
    
    uint8_t rex = 0x00;
    if (xmm >= 8) {
        rex |= 0x44;  // REX.R
        xmm -= 8;
    }
    if (base_reg >= 8) {
        rex |= 0x41;  // REX.B
        base_reg -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x10});
    
    // ModR/M byte
    if (offset == 0 && base_reg != 5) {  // rbp needs offset
        emit(static_cast<uint8_t>((xmm << 3) | base_reg));
    } else if (offset >= -128 && offset <= 127) {
        emit(static_cast<uint8_t>(0x40 | (xmm << 3) | base_reg));
        emit(static_cast<uint8_t>(offset));
    } else {
        emit(static_cast<uint8_t>(0x80 | (xmm << 3) | base_reg));
        emit(static_cast<uint8_t>(offset & 0xFF));
        emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
        emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
        emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
    }
}

void CodeBuffer::emit_movsd_mem_xmm(int base_reg, int32_t offset, int xmm) {
    // movsd [base_reg + offset], xmm
    // F2 0F 11 /r
    
    uint8_t rex = 0x00;
    if (xmm >= 8) {
        rex |= 0x44;  // REX.R
        xmm -= 8;
    }
    if (base_reg >= 8) {
        rex |= 0x41;  // REX.B
        base_reg -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x11});
    
    // ModR/M byte
    if (offset == 0 && base_reg != 5) {
        emit(static_cast<uint8_t>((xmm << 3) | base_reg));
    } else if (offset >= -128 && offset <= 127) {
        emit(static_cast<uint8_t>(0x40 | (xmm << 3) | base_reg));
        emit(static_cast<uint8_t>(offset));
    } else {
        emit(static_cast<uint8_t>(0x80 | (xmm << 3) | base_reg));
        emit(static_cast<uint8_t>(offset & 0xFF));
        emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
        emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
        emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
    }
}

void CodeBuffer::emit_movsd_xmm_xmm(int dst, int src) {
    // movsd dst, src (actually movaps for reg-to-reg)
    uint8_t rex = 0x00;
    if (dst >= 8) {
        rex |= 0x44;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x41;
        src -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x10, static_cast<uint8_t>(0xC0 | (dst << 3) | src)});
}

void CodeBuffer::emit_addsd(int dst, int src) {
    // addsd dst, src
    uint8_t rex = 0x00;
    if (dst >= 8) {
        rex |= 0x44;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x41;
        src -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x58, static_cast<uint8_t>(0xC0 | (dst << 3) | src)});
}

void CodeBuffer::emit_subsd(int dst, int src) {
    // subsd dst, src
    uint8_t rex = 0x00;
    if (dst >= 8) {
        rex |= 0x44;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x41;
        src -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x5C, static_cast<uint8_t>(0xC0 | (dst << 3) | src)});
}

void CodeBuffer::emit_mulsd(int dst, int src) {
    // mulsd dst, src
    uint8_t rex = 0x00;
    if (dst >= 8) {
        rex |= 0x44;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x41;
        src -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x59, static_cast<uint8_t>(0xC0 | (dst << 3) | src)});
}

void CodeBuffer::emit_divsd(int dst, int src) {
    // divsd dst, src
    uint8_t rex = 0x00;
    if (dst >= 8) {
        rex |= 0x44;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x41;
        src -= 8;
    }
    
    emit(0xF2);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x5E, static_cast<uint8_t>(0xC0 | (dst << 3) | src)});
}

void CodeBuffer::emit_ucomisd(int xmm1, int xmm2) {
    // ucomisd xmm1, xmm2
    uint8_t rex = 0x00;
    if (xmm1 >= 8) {
        rex |= 0x44;
        xmm1 -= 8;
    }
    if (xmm2 >= 8) {
        rex |= 0x41;
        xmm2 -= 8;
    }
    
    emit(0x66);
    if (rex != 0) emit(rex);
    emit({0x0F, 0x2E, static_cast<uint8_t>(0xC0 | (xmm1 << 3) | xmm2)});
}

void CodeBuffer::emit_mov_reg_imm64(int reg, int64_t imm) {
    // mov reg, imm64 (REX.W + B8+rd id)
    uint8_t rex = 0x48;  // REX.W
    if (reg >= 8) {
        rex |= 0x01;  // REX.B
        reg -= 8;
    }
    emit(rex);
    emit(static_cast<uint8_t>(0xB8 + reg));
    for (int i = 0; i < 8; ++i) {
        emit(static_cast<uint8_t>((imm >> (i * 8)) & 0xFF));
    }
}

void CodeBuffer::emit_xor_reg_reg(int dst, int src) {
    // xor dst, src
    uint8_t rex = 0x48;  // REX.W
    if (dst >= 8) {
        rex |= 0x04;
        dst -= 8;
    }
    if (src >= 8) {
        rex |= 0x01;
        src -= 8;
    }
    emit(rex);
    emit(0x31);
    emit(static_cast<uint8_t>(0xC0 | (src << 3) | dst));
}

void CodeBuffer::emit_jmp_rel32(int32_t offset) {
    emit(0xE9);
    emit(static_cast<uint8_t>(offset & 0xFF));
    emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
}

void CodeBuffer::emit_jne_rel32(int32_t offset) {
    emit({0x0F, 0x85});
    emit(static_cast<uint8_t>(offset & 0xFF));
    emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
}

void CodeBuffer::emit_je_rel32(int32_t offset) {
    emit({0x0F, 0x84});
    emit(static_cast<uint8_t>(offset & 0xFF));
    emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
    emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
}

void CodeBuffer::emit_test_reg_reg(int reg1, int reg2) {
    uint8_t rex = 0x48;
    if (reg1 >= 8) {
        rex |= 0x04;
        reg1 -= 8;
    }
    if (reg2 >= 8) {
        rex |= 0x01;
        reg2 -= 8;
    }
    emit(rex);
    emit(0x85);
    emit(static_cast<uint8_t>(0xC0 | (reg2 << 3) | reg1));
}

void CodeBuffer::emit_seta(int reg8) {
    if (reg8 >= 4) {
        emit(0x40);  // REX prefix needed for SPL, BPL, SIL, DIL
    }
    emit({0x0F, 0x97, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

void CodeBuffer::emit_setae(int reg8) {
    if (reg8 >= 4) emit(0x40);
    emit({0x0F, 0x93, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

void CodeBuffer::emit_setb(int reg8) {
    if (reg8 >= 4) emit(0x40);
    emit({0x0F, 0x92, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

void CodeBuffer::emit_setbe(int reg8) {
    if (reg8 >= 4) emit(0x40);
    emit({0x0F, 0x96, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

void CodeBuffer::emit_sete(int reg8) {
    if (reg8 >= 4) emit(0x40);
    emit({0x0F, 0x94, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

void CodeBuffer::emit_setne(int reg8) {
    if (reg8 >= 4) emit(0x40);
    emit({0x0F, 0x95, static_cast<uint8_t>(0xC0 | (reg8 & 7))});
}

auto CodeBuffer::position() const -> size_t {
    return code_.size();
}

void CodeBuffer::patch_rel32(size_t pos, int32_t offset) {
    code_[pos] = static_cast<uint8_t>(offset & 0xFF);
    code_[pos + 1] = static_cast<uint8_t>((offset >> 8) & 0xFF);
    code_[pos + 2] = static_cast<uint8_t>((offset >> 16) & 0xFF);
    code_[pos + 3] = static_cast<uint8_t>((offset >> 24) & 0xFF);
}

auto CodeBuffer::finalize() -> JitFunction {
    if (code_.empty()) {
        return nullptr;
    }
    
#ifdef __linux__
    executable_size_ = code_.size();
    executable_ = mmap(nullptr, executable_size_,
                       PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (executable_ == MAP_FAILED) {
        executable_ = nullptr;
        return nullptr;
    }
    std::memcpy(executable_, code_.data(), code_.size());
#endif

#ifdef _WIN32
    executable_size_ = code_.size();
    executable_ = VirtualAlloc(nullptr, executable_size_,
                               MEM_COMMIT | MEM_RESERVE,
                               PAGE_EXECUTE_READWRITE);
    if (executable_ == nullptr) {
        return nullptr;
    }
    std::memcpy(executable_, code_.data(), code_.size());
#endif

    return reinterpret_cast<JitFunction>(executable_);
}

// ============================================================================
// JitCompiler implementation
// ============================================================================

JitCompiler::JitCompiler() = default;

auto JitCompiler::is_supported() -> bool {
#if defined(__x86_64__) || defined(_M_X64)
    return true;
#else
    return false;
#endif
}

void JitCompiler::emit_prologue(int num_locals) {
    buffer_.emit_push_rbp();
    buffer_.emit_mov_rbp_rsp();
    
    // Allocate stack space for locals (16-byte aligned)
    stack_size_ = ((num_locals * 8) + 15) & ~15;
    if (stack_size_ > 0) {
        // sub rsp, stack_size_
        buffer_.emit({0x48, 0x81, 0xEC});
        buffer_.emit(static_cast<uint8_t>(stack_size_ & 0xFF));
        buffer_.emit(static_cast<uint8_t>((stack_size_ >> 8) & 0xFF));
        buffer_.emit(static_cast<uint8_t>((stack_size_ >> 16) & 0xFF));
        buffer_.emit(static_cast<uint8_t>((stack_size_ >> 24) & 0xFF));
    }
}

void JitCompiler::emit_epilogue() {
    buffer_.emit_mov_rsp_rbp();
    buffer_.emit_pop_rbp();
    buffer_.emit_ret();
}

auto JitCompiler::get_value_offset(const ir::SsaValue& value) -> int32_t {
    uint64_t key = (static_cast<uint64_t>(value.symbol) << 32) | value.version;
    auto it = value_offsets_.find(key);
    if (it != value_offsets_.end()) {
        return it->second;
    }
    // Allocate new slot
    allocate_value(value);
    return value_offsets_[key];
}

void JitCompiler::allocate_value(const ir::SsaValue& value) {
    uint64_t key = (static_cast<uint64_t>(value.symbol) << 32) | value.version;
    int32_t offset = -static_cast<int32_t>((value_offsets_.size() + 1) * 8);
    value_offsets_[key] = offset;
}

void JitCompiler::load_value_to_xmm(int xmm, const ir::SsaValue& value) {
    int32_t offset = get_value_offset(value);
    buffer_.emit_movsd_xmm_mem(xmm, static_cast<int>(Register::RBP), offset);
}

void JitCompiler::store_xmm_to_value(const ir::SsaValue& value, int xmm) {
    int32_t offset = get_value_offset(value);
    buffer_.emit_movsd_mem_xmm(static_cast<int>(Register::RBP), offset, xmm);
}

void JitCompiler::compile_instruction(const ir::SsaInstruction& inst) {
    if (inst.opcode == "literal") {
        // Load immediate double value
        double val = 0.0;
        if (!inst.immediates.empty()) {
            try {
                val = std::stod(inst.immediates[0]);
            } catch (...) {
                val = 0.0;
            }
        }
        
        // Move immediate to memory via register
        int64_t bits;
        std::memcpy(&bits, &val, sizeof(bits));
        buffer_.emit_mov_reg_imm64(static_cast<int>(Register::RAX), bits);
        
        // mov [rbp + offset], rax
        if (inst.result.has_value()) {
            int32_t offset = get_value_offset(*inst.result);
            buffer_.emit({0x48, 0x89, 0x85});  // mov [rbp + disp32], rax
            buffer_.emit(static_cast<uint8_t>(offset & 0xFF));
            buffer_.emit(static_cast<uint8_t>((offset >> 8) & 0xFF));
            buffer_.emit(static_cast<uint8_t>((offset >> 16) & 0xFF));
            buffer_.emit(static_cast<uint8_t>((offset >> 24) & 0xFF));
        }
    }
    else if (inst.opcode == "binary") {
        if (inst.arguments.size() < 2 || inst.immediates.empty()) {
            return;
        }
        
        load_value_to_xmm(0, inst.arguments[0]);
        load_value_to_xmm(1, inst.arguments[1]);
        
        const std::string& op = inst.immediates[0];
        
        if (op == "+") {
            buffer_.emit_addsd(0, 1);
        } else if (op == "-") {
            buffer_.emit_subsd(0, 1);
        } else if (op == "*") {
            buffer_.emit_mulsd(0, 1);
        } else if (op == "/") {
            buffer_.emit_divsd(0, 1);
        } else if (op == "<" || op == "<=" || op == ">" || op == ">=" ||
                   op == "==" || op == "!=") {
            buffer_.emit_ucomisd(0, 1);
            buffer_.emit_xor_reg_reg(static_cast<int>(Register::RAX),
                                     static_cast<int>(Register::RAX));
            
            if (op == "<") {
                buffer_.emit_setb(0);  // al = (xmm0 < xmm1)
            } else if (op == "<=") {
                buffer_.emit_setbe(0);
            } else if (op == ">") {
                buffer_.emit_seta(0);
            } else if (op == ">=") {
                buffer_.emit_setae(0);
            } else if (op == "==") {
                buffer_.emit_sete(0);
            } else if (op == "!=") {
                buffer_.emit_setne(0);
            }
            
            // Convert al to double in xmm0
            // cvtsi2sd xmm0, rax
            buffer_.emit({0xF2, 0x48, 0x0F, 0x2A, 0xC0});
        }
        
        if (inst.result.has_value()) {
            store_xmm_to_value(*inst.result, 0);
        }
    }
    else if (inst.opcode == "assign") {
        if (!inst.arguments.empty() && inst.result.has_value()) {
            load_value_to_xmm(0, inst.arguments[0]);
            store_xmm_to_value(*inst.result, 0);
        }
    }
    else if (inst.opcode == "return") {
        if (!inst.arguments.empty()) {
            load_value_to_xmm(0, inst.arguments[0]);
        }
        emit_epilogue();
    }
}

void JitCompiler::compile_block(const ir::SsaBlock& block) {
    label_positions_[block.name] = buffer_.position();
    
    // Process phi nodes (simplified - just copy first incoming value for now)
    for (const auto& phi : block.phi_nodes) {
        if (!phi.inputs.empty() && phi.inputs[0].value.has_value()) {
            load_value_to_xmm(0, *phi.inputs[0].value);
            store_xmm_to_value(phi.result, 0);
        }
    }
    
    // Compile instructions
    for (const auto& inst : block.instructions) {
        compile_instruction(inst);
    }
}

auto JitCompiler::compile(const ir::SsaFunction& function) -> JitFunction {
    if (!is_supported()) {
        return nullptr;
    }
    
    // Reset state
    value_offsets_.clear();
    label_positions_.clear();
    pending_jumps_.clear();
    buffer_ = CodeBuffer{};
    
    // Count locals (rough estimate)
    int num_locals = 0;
    for (const auto& block : function.blocks) {
        num_locals += static_cast<int>(block.phi_nodes.size());
        num_locals += static_cast<int>(block.instructions.size());
    }
    
    emit_prologue(num_locals);
    
    // Store function parameters (RDI contains pointer to args array)
    // For now, just compile the blocks
    for (const auto& block : function.blocks) {
        compile_block(block);
    }
    
    // Patch jumps
    for (const auto& [pos, label] : pending_jumps_) {
        auto it = label_positions_.find(label);
        if (it != label_positions_.end()) {
            int32_t offset = static_cast<int32_t>(it->second - pos - 4);
            buffer_.patch_rel32(pos, offset);
        }
    }
    
    return buffer_.finalize();
}

}  // namespace impulse::jit
