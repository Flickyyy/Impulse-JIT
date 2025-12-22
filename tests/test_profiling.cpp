#include <gtest/gtest.h>
#include <iostream>
#include <sstream>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/runtime.h"

using namespace impulse::runtime;

// Example: Profiling a program to identify bottlenecks
TEST(ProfilingTest, BasicProfiling) {
    const std::string source = R"(module test;

func compute(x: float) -> float {
    // This function will be JIT compiled (pure arithmetic)
    return x * x + x * 2.0 - 1.0;
}

func accumulate(n: float) -> float {
    // This function uses loops, so won't be JIT compiled
    let sum: float = 0.0;
    let i: float = 0.0;
    while i < n {
        sum = sum + compute(i);
        i = i + 1.0;
    }
    return sum;
}

func main() -> float {
    return accumulate(1000.0);
}
)";

    // Parse and load the module
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success);
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    ASSERT_TRUE(semantic.success);
    
    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    
    // Create VM and enable profiling
    Vm vm;
    vm.set_jit_enabled(true);
    vm.set_profiling_enabled(true);
    vm.reset_profiling();  // Start fresh
    
    // Load and run
    auto load_result = vm.load(lowered);
    ASSERT_TRUE(load_result.success);
    
    std::string module_name = "test";
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) name_stream << "::";
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    }
    
    auto result = vm.run(module_name, "main");
    EXPECT_EQ(result.status, VmStatus::Success);
    
    // Get and display profiling results
    std::string profiling_output = vm.get_profiling_results();
    std::cout << "\n" << profiling_output << "\n";
    
    // Verify profiling data was collected
    EXPECT_FALSE(profiling_output.empty());
    EXPECT_NE(profiling_output.find("Function Profiling Results"), std::string::npos);
    
    // The compute function should be JIT compiled
    EXPECT_TRUE(vm.is_function_jit_compiled(module_name, "compute"));
    
    // The accumulate function should not be JIT compiled (has loops)
    EXPECT_FALSE(vm.is_function_jit_compiled(module_name, "accumulate"));
}

// Example: Comparing JIT vs interpreter performance with profiling
TEST(ProfilingTest, JitVsInterpreterProfiling) {
    const std::string source = R"(module test;

func fast_math(x: float) -> float {
    // JIT compiled function
    return x * x + x * 2.0 - 1.0;
}

func main() -> float {
    let total: float = 0.0;
    let i: float = 0.0;
    while i < 10000.0 {
        total = total + fast_math(i);
        i = i + 1.0;
    }
    return total;
}
)";

    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    ASSERT_TRUE(parse_result.success);
    
    const auto semantic = impulse::frontend::analyzeModule(parse_result.module);
    ASSERT_TRUE(semantic.success);
    
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
    
    // Test with JIT enabled
    {
        Vm vm;
        vm.set_jit_enabled(true);
        vm.set_profiling_enabled(true);
        vm.reset_profiling();
        
        auto load_result = vm.load(lowered);
        ASSERT_TRUE(load_result.success);
        
        auto result = vm.run(module_name, "main");
        EXPECT_EQ(result.status, VmStatus::Success);
        
        std::cout << "\n=== With JIT Enabled ===\n";
        std::cout << vm.get_profiling_results() << "\n";
    }
    
    // Test with JIT disabled
    {
        Vm vm;
        vm.set_jit_enabled(false);
        vm.set_profiling_enabled(true);
        vm.reset_profiling();
        
        auto load_result = vm.load(lowered);
        ASSERT_TRUE(load_result.success);
        
        auto result = vm.run(module_name, "main");
        EXPECT_EQ(result.status, VmStatus::Success);
        
        std::cout << "\n=== With JIT Disabled ===\n";
        std::cout << vm.get_profiling_results() << "\n";
    }
}

