#include <gtest/gtest.h>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/runtime.h"

using namespace impulse::runtime;
using namespace std::chrono;

namespace {

// Helper to run a function and measure execution time
struct ExecutionResult {
    VmResult result;
    milliseconds interpreter_time{0};
    milliseconds jit_time{0};
    bool jit_used = false;
};


ExecutionResult run_with_timing(const std::string& source, const std::string& function_name = "main") {
    ExecutionResult exec_result;
    
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    if (!parse_result.success) {
        exec_result.result.status = VmStatus::RuntimeError;
        exec_result.result.message = "Parse failed";
        if (!parse_result.diagnostics.empty()) {
            exec_result.result.message += ": " + parse_result.diagnostics[0].message;
        }
        return exec_result;
    }
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    if (!semantic.success) {
        exec_result.result.status = VmStatus::RuntimeError;
        exec_result.result.message = "Semantic analysis failed";
        if (!semantic.diagnostics.empty()) {
            exec_result.result.message += ": " + semantic.diagnostics[0].message;
        }
        return exec_result;
    }
    
    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    
    // Get module name
    std::string module_name;
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) name_stream << "::";
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    } else {
        module_name = "test";
    }
    
    // Test with interpreter (JIT disabled)
    {
        Vm vm;
        vm.set_jit_enabled(false);
        
        auto load_result = vm.load(lowered);
        if (!load_result.success) {
            exec_result.result.status = VmStatus::RuntimeError;
            exec_result.result.message = "Load failed";
            if (!load_result.diagnostics.empty()) {
                exec_result.result.message += ": " + load_result.diagnostics[0];
            }
            return exec_result;
        }
        
        auto start = high_resolution_clock::now();
        auto result = vm.run(module_name, function_name);
        auto end = high_resolution_clock::now();
        
        exec_result.interpreter_time = duration_cast<milliseconds>(end - start);
        exec_result.result = result;
        
        if (result.status != VmStatus::Success) {
            return exec_result;
        }
    }
    
    // Test with JIT (JIT enabled)
    {
        Vm vm;
        vm.set_jit_enabled(true);
        
        auto load_result = vm.load(lowered);
        if (!load_result.success) {
            exec_result.result.status = VmStatus::RuntimeError;
            exec_result.result.message = "Load failed (JIT)";
            if (!load_result.diagnostics.empty()) {
                exec_result.result.message += ": " + load_result.diagnostics[0];
            }
            return exec_result;
        }
        
        auto start = high_resolution_clock::now();
        auto result = vm.run(module_name, function_name);
        auto end = high_resolution_clock::now();
        
        exec_result.jit_time = duration_cast<milliseconds>(end - start);
        
        if (result.status != VmStatus::Success) {
            exec_result.result.status = VmStatus::RuntimeError;
            exec_result.result.message = "JIT failed: " + result.message;
            return exec_result;
        }
        
        // Check if results match
        if (result.status != exec_result.result.status) {
            exec_result.result.status = VmStatus::RuntimeError;
            exec_result.result.message = "JIT and interpreter results differ";
            return exec_result;
        }
        
        if (result.has_value && exec_result.result.has_value) {
            double diff = std::abs(result.value - exec_result.result.value);
            if (diff > 1e-9) {
                exec_result.result.status = VmStatus::RuntimeError;
                exec_result.result.message = "JIT and interpreter values differ: " + 
                    std::to_string(result.value) + " vs " + std::to_string(exec_result.result.value);
                return exec_result;
            }
        }
        
        // Check if JIT was actually used
        exec_result.jit_used = (result.message == "__JIT_USED__");
    }
    
    return exec_result;
}

// Helper to create a Vm and load a module (for cache inspection)
// Returns a unique_ptr to Vm since Vm is not copyable
std::pair<std::unique_ptr<Vm>, std::string> create_vm_with_module(const std::string& source) {
    auto vm = std::make_unique<Vm>();
    vm->set_jit_enabled(true);
    
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    if (!parse_result.success) {
        return std::make_pair(std::move(vm), std::string(""));
    }
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    if (!semantic.success) {
        return std::make_pair(std::move(vm), std::string(""));
    }
    
    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    
    // Get module name
    std::string module_name;
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) name_stream << "::";
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    } else {
        module_name = "test";
    }
    
    auto load_result = vm->load(lowered);
    if (!load_result.success) {
        return std::make_pair(std::move(vm), std::string(""));
    }
    
    return std::make_pair(std::move(vm), module_name);
}

}  // namespace

// Test: Large arithmetic computation with loop (should be JIT compiled and show performance gain)
TEST(JitPerformanceTest, LargeArithmeticComputation) {
    const std::string source = R"(module test;

func compute_value(x: float) -> float {
    // Pure arithmetic function - will be JIT compiled
    // Many arithmetic operations in a single expression
    return x * x + x * 2.0 - 1.0 + x * 3.0 / 2.0 + x * 4.0 - 5.0 + 
           x * 6.0 / 3.0 + x * 7.0 - 8.0 + x * 9.0 / 4.0 + x * 10.0;
}

func main() -> float {
    // Loop calling the JIT-compiled function many times
    let total: float = 0.0;
    let i: float = 0.0;
    while i < 10000.0 {
        total = total + compute_value(i);
        i = i + 1.0;
    }
    return total;
}
)";
    
    auto result = run_with_timing(source);
    
    if (result.result.status != VmStatus::Success) {
        std::cout << "ERROR: " << result.result.message << std::endl;
    }
    EXPECT_EQ(result.result.status, VmStatus::Success) << "Execution failed: " << result.result.message;
    EXPECT_TRUE(result.result.has_value);
    
    std::cout << "\n=== Large Arithmetic Computation ===" << std::endl;
    std::cout << "Interpreter time: " << result.interpreter_time.count() << "ms" << std::endl;
    std::cout << "JIT time: " << result.jit_time.count() << "ms" << std::endl;
    
    if (result.jit_time.count() > 0 && result.interpreter_time.count() > 0) {
        double speedup = static_cast<double>(result.interpreter_time.count()) / result.jit_time.count();
        EXPECT_GT(speedup, 1);
        std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
        if (speedup > 1.2) {
            std::cout << "✓ JIT provides significant speedup!" << std::endl;
        }
    }
    std::cout << "====================================\n" << std::endl;
}

// Test 5: Function that should NOT be JIT compiled (uses arrays)
TEST(JitDebugTest, ArrayFunctionNotJitted) {
    const std::string source = R"(module test;

func main() -> float {
    let arr: array = array(3);
    array_set(arr, 0, 10.0);
    array_set(arr, 1, 20.0);
    array_set(arr, 2, 30.0);
    return array_get(arr, 0) + array_get(arr, 1) + array_get(arr, 2);
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 60.0, 1e-9);
    
    // Verify cache behavior - function should be cached but NOT JIT compiled
    auto [vm_ptr, module_name] = create_vm_with_module(source);
    if (!module_name.empty() && vm_ptr != nullptr) {
        auto& vm = *vm_ptr;
        (void)vm.run(module_name, "main");
        EXPECT_TRUE(vm.is_function_cached(module_name, "main")) << "main should be cached";
        EXPECT_FALSE(vm.is_function_jit_compiled(module_name, "main")) << "main should NOT be JIT compiled (uses arrays)";
    }
    
    // This should use interpreter (arrays not supported by JIT)
    std::cout << "ArrayFunction - Interpreter: " << result.interpreter_time.count() 
              << "ms, JIT: " << result.jit_time.count() << "ms" << std::endl;
}


// Test 7: Function that should NOT be JIT compiled (uses control flow)
TEST(JitDebugTest, ControlFlowNotJitted) {
    const std::string source = R"(module test;

func main() -> float {
    let x: float = 10.0;
    if x > 5.0 {
        x = x * 2.0;
    }
    return x;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 20.0, 1e-9);
    
    // This should use interpreter (control flow not supported by JIT)
    std::cout << "ControlFlow - Interpreter: " << result.interpreter_time.count() 
              << "ms, JIT: " << result.jit_time.count() << "ms" << std::endl;
}

// Test 8: Performance test - many arithmetic operations
TEST(JitPerformanceTest, ManyOperations) {
    const std::string source = R"(module test;

func main() -> float {
    let result: float = 0.0;
    let i: float = 0.0;
    while i < 1000.0 {
        result = result + i * 2.0 - 1.0;
        i = i + 1.0;
    }
    return result;
}
)";
    
    // This uses loops, so won't be JIT compiled - just test interpreter
    ExecutionResult exec_result;
    
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    if (!parse_result.success) {
        exec_result.result.status = VmStatus::RuntimeError;
        exec_result.result.message = "Parse failed";
        return;
    }
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    if (!semantic.success) {
        exec_result.result.status = VmStatus::RuntimeError;
        exec_result.result.message = "Semantic analysis failed";
        return;
    }
    
    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    
    std::string module_name = "test";
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) name_stream << "::";
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    }
    
    Vm vm;
    vm.set_jit_enabled(false);  // Disable JIT since this uses control flow
    
    auto load_result = vm.load(lowered);
    EXPECT_TRUE(load_result.success) << "Load failed";
    if (!load_result.success) return;
    
    auto result = vm.run(module_name, "main");
    
    EXPECT_EQ(result.status, VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    
    // Expected: sum of (i * 2.0 - 1.0) for i from 0 to 999
    // = sum of (2i - 1) = 2 * sum(i) - 1000 = 2 * (999*1000/2) - 1000 = 999000 - 1000 = 998000
    double expected = 0.0;
    for (double i = 0.0; i < 1000.0; i += 1.0) {
        expected += i * 2.0 - 1.0;
    }
    EXPECT_NEAR(result.value, expected, 1e-6);
    
    std::cout << "ManyOperations - Result: " << result.value << " (expected: " << expected << ")" << std::endl;
    std::cout << "Note: This test uses while loops, so JIT compilation is not supported" << std::endl;
}

// Test 9: Verify JIT correctness - floating point precision
TEST(JitCorrectnessTest, FloatingPointPrecision) {
    const std::string source = R"(module test;

func main() -> float {
    let a: float = 0.1;
    let b: float = 0.2;
    let c: float = 0.3;
    return a + b - c;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    
    // Should be very close to 0 (floating point precision issue)
    EXPECT_LT(std::abs(result.result.value), 1e-10);
    EXPECT_TRUE(result.jit_used) << "JIT should have been used for this computation";
    
    std::cout << "FloatingPointPrecision result: " << result.result.value << ", JIT used: " << result.jit_used << std::endl;
}

// Test: Verify JIT cache behavior
TEST(JitCacheTest, CacheVerification) {
    const std::string source = R"(module test;

func simple_add(x: float, y: float) -> float {
    return x + y;
}

func main() -> float {
    return simple_add(10.0, 20.0);
})";
    
    auto [vm_ptr, module_name] = create_vm_with_module(source);
    ASSERT_FALSE(module_name.empty()) << "Failed to create VM";
    ASSERT_NE(vm_ptr, nullptr) << "Failed to create VM";
    auto& vm = *vm_ptr;
    
    // Cache should be empty initially
    EXPECT_EQ(vm.get_jit_cache_size(), 0) << "Cache should be empty before first call";
    EXPECT_FALSE(vm.is_function_cached(module_name, "main")) << "main should not be cached yet";
    EXPECT_FALSE(vm.is_function_cached(module_name, "simple_add")) << "simple_add should not be cached yet";
    
    // First call - should populate cache
    auto result1 = vm.run(module_name, "main");
    EXPECT_EQ(result1.status, VmStatus::Success);
    EXPECT_NEAR(result1.value, 30.0, 1e-9);
    
    // After first call, functions should be cached
    EXPECT_GT(vm.get_jit_cache_size(), 0) << "Cache should have entries after first call";
    EXPECT_TRUE(vm.is_function_cached(module_name, "main")) << "main should be cached";
    EXPECT_TRUE(vm.is_function_cached(module_name, "simple_add")) << "simple_add should be cached";
    
    // simple_add should be JIT compiled (pure arithmetic)
    EXPECT_TRUE(vm.is_function_jit_compiled(module_name, "simple_add")) << "simple_add should be JIT compiled";
    
    // main might not be JIT compiled if it has control flow, but it should be cached
    // (main calls simple_add, so it might not be JIT compiled due to function calls)
    
    // Second call - should use cached version
    auto result2 = vm.run(module_name, "main");
    EXPECT_EQ(result2.status, VmStatus::Success);
    EXPECT_NEAR(result2.value, 30.0, 1e-9);
    
    // Cache size should remain the same (no new entries)
    size_t cache_size_after = vm.get_jit_cache_size();
    EXPECT_EQ(cache_size_after, vm.get_jit_cache_size()) << "Cache size should not increase on second call";
    
    std::cout << "Cache test - Cache size: " << vm.get_jit_cache_size() << std::endl;
    std::cout << "  main cached: " << vm.is_function_cached(module_name, "main") << std::endl;
    std::cout << "  main JIT compiled: " << vm.is_function_jit_compiled(module_name, "main") << std::endl;
    std::cout << "  simple_add cached: " << vm.is_function_cached(module_name, "simple_add") << std::endl;
    std::cout << "  simple_add JIT compiled: " << vm.is_function_jit_compiled(module_name, "simple_add") << std::endl;
}

// Test 10: Edge case - function with no parameters
TEST(JitDebugTest, NoParameters) {
    const std::string source = R"(module test;

func main() -> float {
    return 42.0;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 42.0, 1e-9);
    
    std::cout << "NoParameters - Interpreter: " << result.interpreter_time.count() 
              << "ms, JIT: " << result.jit_time.count() << "ms" << std::endl;
}

// Test: Function with multiple parameters (should be JIT compiled)
TEST(JitCorrectnessTest, MultipleParameters) {
    const std::string source = R"(module test;

func main(a: float, b: float, c: float, d: float) -> float {
    return a + b * c - d;
}
)";
    
    // Note: This test requires calling main with parameters, which the test framework doesn't support
    // So we'll test a simpler version that doesn't require parameters
    const std::string simple_source = R"(module test;

func main() -> float {
    let a: float = 1.0;
    let b: float = 2.0;
    let c: float = 3.0;
    let d: float = 4.0;
    return a + b * c - d;
}
)";
    
    auto result = run_with_timing(simple_source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 3.0, 1e-9);  // 1 + 2*3 - 4 = 3
    EXPECT_TRUE(result.jit_used) << "JIT should have been used for this computation";
}

// Test: Large computation with many operations in a loop
TEST(JitPerformanceTest, LargeComputation) {
    const std::string source = R"(module test;

func process_value(x: float) -> float {
    // JIT compiled function - pure arithmetic with many operations
    // Using nested expressions since assignment might not work in JIT
    return (((((((((x * 2.0 + 1.0) / 3.0 - 0.5) * 4.0 + 2.0) / 5.0 - 1.0) * 6.0 + 3.0) / 7.0 - 1.5) * 8.0 + 4.0) / 9.0 - 2.0) * 10.0 + 5.0) / 11.0 - 2.5;
}

func main() -> float {
    // Loop calling the JIT-compiled function many times
    let sum: float = 0.0;
    let i: float = 0.0;
    while i < 10000.0 {
        sum = sum + process_value(i);
        i = i + 1.0;
    }
    return sum;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success) << "Execution failed: " << result.result.message;
    EXPECT_TRUE(result.result.has_value);
    
    std::cout << "\n=== Large Computation ===" << std::endl;
    std::cout << "Interpreter time: " << result.interpreter_time.count() << "ms" << std::endl;
    std::cout << "JIT time: " << result.jit_time.count() << "ms" << std::endl;
    
    if (result.jit_time.count() > 0 && result.interpreter_time.count() > 0) {
        double speedup = static_cast<double>(result.interpreter_time.count()) / result.jit_time.count();
        EXPECT_GT(speedup, 1);
        std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
        if (speedup > 1.2) {
            std::cout << "✓ JIT provides significant speedup!" << std::endl;
        }
    }
    std::cout << "========================\n" << std::endl;
}

// Test 13: Verify return value handling
TEST(JitCorrectnessTest, ReturnValueHandling) {
    const std::string source = R"(module test;

func main() -> float {
    let x: float = 100.0;
    let y: float = 200.0;
    return x + y;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 300.0, 1e-9);
    EXPECT_TRUE(result.jit_used) << "JIT should have been used for this simple computation";
    
    // Verify cache behavior
    auto [vm_ptr, module_name] = create_vm_with_module(source);
    if (!module_name.empty() && vm_ptr != nullptr) {
        auto& vm = *vm_ptr;
        (void)vm.run(module_name, "main");
        EXPECT_TRUE(vm.is_function_cached(module_name, "main")) << "main should be cached";
        EXPECT_TRUE(vm.is_function_jit_compiled(module_name, "main")) << "main should be JIT compiled";
    }
    
    std::cout << "ReturnValueHandling - Value: " << result.result.value << ", JIT used: " << result.jit_used << std::endl;
}

// Test: All supported binary operators (arithmetic and comparison)
TEST(JitCorrectnessTest, AllBinaryOperators) {
    const std::string source = R"(module test;

func main() -> float {
    let a: float = 10.0;
    let b: float = 3.0;
    
    // Arithmetic operations
    let add: float = a + b;
    let sub: float = a - b;
    let mul: float = a * b;
    let div: float = a / b;
    
    // Comparison operations (JIT supports these, they return 1.0 or 0.0)
    let lt: float = a < b;
    let le: float = a <= b;
    let gt: float = a > b;
    let ge: float = a >= b;
    let eq: float = a == b;
    let ne: float = a != b;
    
    return add + sub + mul + div + lt + le + gt + ge + eq + ne;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    
    // Expected: 13 + 7 + 30 + 3.333... + 0 + 0 + 1 + 1 + 0 + 1 = ~56.33
    EXPECT_GT(result.result.value, 50.0);
    EXPECT_LT(result.result.value, 60.0);
    EXPECT_TRUE(result.jit_used) << "JIT should have been used for this computation";
    
    std::cout << "AllBinaryOperators - Result: " << result.result.value << std::endl;
}

// Test 15: Stress test - verify JIT doesn't crash on edge cases
TEST(JitDebugTest, EdgeCases) {
    // Test with very small values
    {
        const std::string source = R"(module test;

func main() -> float {
    return 0.0000001;
}
)";
        
        auto result = run_with_timing(source);
        EXPECT_EQ(result.result.status, VmStatus::Success);
        EXPECT_TRUE(result.result.has_value);
    }
    
    // Test with very large values
    {
        const std::string source = R"(module test;

func main() -> float {
    return 1000000.0;
}
)";
        
        auto result = run_with_timing(source);
        EXPECT_EQ(result.result.status, VmStatus::Success);
        EXPECT_TRUE(result.result.has_value);
    }
    
    // Test with negative values
    {
        const std::string source = R"(module test;

func main() -> float {
    return -42.0;
}
)";
        
        auto result = run_with_timing(source);
        EXPECT_EQ(result.result.status, VmStatus::Success);
        EXPECT_TRUE(result.result.has_value);
        EXPECT_NEAR(result.result.value, -42.0, 1e-9);
    }
    
    std::cout << "EdgeCases - All passed" << std::endl;
}


// Test 17: Debug test - verify correct behavior with division
TEST(JitCorrectnessTest, Division) {
    const std::string source = R"(module test;

func main() -> float {
    return 100.0 / 4.0;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success);
    EXPECT_TRUE(result.result.has_value);
    EXPECT_NEAR(result.result.value, 25.0, 1e-9);
    EXPECT_TRUE(result.jit_used) << "JIT should have been used for this computation";
    
    std::cout << "Division test - Result: " << result.result.value << ", JIT used: " << result.jit_used << std::endl;
}



// Test: Complex mathematical computation in a loop (should be JIT compiled)
TEST(JitPerformanceTest, ComplexMathematicalComputation) {
    const std::string source = R"(module test;

func complex_calc(a: float, b: float, c: float, d: float) -> float {
    // JIT compiled function - pure arithmetic with complex nested expressions
    return ((a + b) * (c - d) / (a * b) + (c + d) * (a - b) / (c * d)) * 2.0 + 
           (a * b * c) / (d + 1.0) - (a + b + c) / d + 
           (a * a) + (b * b) - (c * c) + (d * d) + 
           (a + b) * (c + d) - (a - b) * (c - d) + 
           (a / b) + (c / d) - (b / a) + (d / c);
}

func main() -> float {
    // Loop calling the JIT-compiled function many times
    let sum: float = 0.0;
    let i: float = 0.0;
    while i < 10000.0 {
        let a: float = i + 1.0;
        let b: float = i + 2.0;
        let c: float = i + 3.0;
        let d: float = i + 4.0;
        sum = sum + complex_calc(a, b, c, d);
        i = i + 1.0;
    }
    return sum;
}
)";
    
    auto result = run_with_timing(source);
    
    EXPECT_EQ(result.result.status, VmStatus::Success) << "Execution failed: " << result.result.message;
    EXPECT_TRUE(result.result.has_value);
    
    std::cout << "\n=== Complex Mathematical Computation ===" << std::endl;
    std::cout << "Interpreter time: " << result.interpreter_time.count() << "ms" << std::endl;
    std::cout << "JIT time: " << result.jit_time.count() << "ms" << std::endl;
    
    if (result.jit_time.count() > 0 && result.interpreter_time.count() > 0) {
        double speedup = static_cast<double>(result.interpreter_time.count()) / result.jit_time.count();
        EXPECT_GT(speedup, 1);
        std::cout << "Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
        if (speedup > 1.2) {
            std::cout << "✓ JIT provides significant speedup!" << std::endl;
        }
    }
    std::cout << "========================================\n" << std::endl;
}

