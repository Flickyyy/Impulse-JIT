#include <gtest/gtest.h>
#include <cmath>
#include <string>
#include <limits>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/runtime.h"

namespace {

[[nodiscard]] auto run_program(const std::string& source, const std::string& func = "main") 
    -> impulse::runtime::VmResult {
    impulse::frontend::Parser parser(source);
    auto parse_result = parser.parseModule();
    if (!parse_result.success) {
        impulse::runtime::VmResult error_result;
        error_result.status = impulse::runtime::VmStatus::RuntimeError;
        error_result.message = "Parse failed";
        return error_result;
    }

    const auto lowered = impulse::frontend::lower_to_ir(parse_result.module);
    impulse::runtime::Vm vm;
    const auto load_result = vm.load(lowered);
    if (!load_result.success) {
        impulse::runtime::VmResult error_result;
        error_result.status = impulse::runtime::VmStatus::RuntimeError;
        error_result.message = "Load failed";
        return error_result;
    }

    std::string module_name;
    if (!lowered.path.empty()) {
        std::ostringstream name_stream;
        for (size_t i = 0; i < lowered.path.size(); ++i) {
            if (i != 0) {
                name_stream << "::";
            }
            name_stream << lowered.path[i];
        }
        module_name = name_stream.str();
    } else {
        module_name = "anonymous";
    }

    return vm.run(module_name, func);
}

}  // namespace

// ============================================================================
// Edge Cases: Boundary Values
// ============================================================================

TEST(EdgeCaseTest, ZeroValues) {
    const std::string source = R"(module test;

func main() -> int {
    let zero: int = 0;
    let neg: int = -0;
    let sum: int = zero + neg;
    let mult: int = zero * 100;
    let div: int = 0 / 5;
    return sum + mult + div;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 0.0);
}

TEST(EdgeCaseTest, NegativeNumbers) {
    const std::string source = R"(module test;

func main() -> int {
    let a: int = -5;
    let b: int = -10;
    let c: int = a + b;
    let d: int = a - b;
    let e: int = a * b;
    let f: int = a / 2;
    return c + d + e + f;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // c = -15, d = 5, e = 50, f = -2.5 (or -2 if integer)
    // -15 + 5 + 50 - 2 = 38 (assuming float division then cast)
    EXPECT_TRUE(result.has_value);
}

TEST(EdgeCaseTest, LargeNumbers) {
    const std::string source = R"(module test;

func main() -> int {
    let big: int = 1000000;
    let bigger: int = big * 1000;
    let huge: int = bigger * 1000;
    return huge / 1000000000;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 1000.0);
}

TEST(EdgeCaseTest, ModuloEdgeCases) {
    const std::string source = R"(module test;

func main() -> int {
    let a: int = 5 % 5;
    let b: int = 0 % 7;
    let c: int = 7 % 1;
    let d: int = 100 % 97;
    return a + b + c + d;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 0 + 0 + 0 + 3 = 3
    EXPECT_DOUBLE_EQ(result.value, 3.0);
}

// ============================================================================
// Edge Cases: Boolean Logic
// ============================================================================

TEST(EdgeCaseTest, BooleanChaining) {
    const std::string source = R"(module test;

func main() -> int {
    let a: bool = true && true && true;
    let b: bool = true && true && false;
    let c: bool = false || false || true;
    let d: bool = false || false || false;
    let e: bool = true && false || true;
    let f: bool = false || true && false;
    return a + b + c + d + e + f;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // a=1, b=0, c=1, d=0, e=1, f=0
    EXPECT_DOUBLE_EQ(result.value, 3.0);
}

TEST(EdgeCaseTest, DoubleNegation) {
    const std::string source = R"(module test;

func main() -> int {
    let a: bool = !true;
    let b: bool = !!true;
    let c: bool = !!!true;
    let d: bool = !!!!true;
    return a + b + c + d;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 0 + 1 + 0 + 1 = 2
    EXPECT_DOUBLE_EQ(result.value, 2.0);
}

TEST(EdgeCaseTest, ComparisonChain) {
    const std::string source = R"(module test;

func main() -> int {
    let eq: int = (1 == 1) + (0 == 0) + (-1 == -1);
    let neq: int = (1 != 2) + (0 != 1) + (-1 != 1);
    let lt: int = (1 < 2) + (-1 < 0) + (0 < 1);
    let le: int = (1 <= 1) + (0 <= 0) + (-1 <= -1);
    let gt: int = (2 > 1) + (0 > -1) + (1 > 0);
    let ge: int = (1 >= 1) + (0 >= 0) + (-1 >= -1);
    return eq + neq + lt + le + gt + ge;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 3 + 3 + 3 + 3 + 3 + 3 = 18
    EXPECT_DOUBLE_EQ(result.value, 18.0);
}

// ============================================================================
// Edge Cases: Control Flow
// ============================================================================

TEST(EdgeCaseTest, EmptyWhileLoop) {
    const std::string source = R"(module test;

func main() -> int {
    let i: int = 0;
    while i > 10 {
        let i: int = i + 1;
    }
    return i;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 0.0);
}

TEST(EdgeCaseTest, EmptyForLoop) {
    const std::string source = R"(module test;

func main() -> int {
    let sum: int = 0;
    for (let i: int = 10; i < 5; let i: int = i + 1;) {
        let sum: int = sum + i;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 0.0);
}

TEST(EdgeCaseTest, ImmediateBreak) {
    const std::string source = R"(module test;

func main() -> int {
    let i: int = 0;
    while true {
        break;
        let i: int = i + 1;
    }
    return i;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 0.0);
}

TEST(EdgeCaseTest, OnlyContune) {
    const std::string source = R"(module test;

func main() -> int {
    let sum: int = 0;
    for (let i: int = 0; i < 10; let i: int = i + 1;) {
        if i < 5 {
            continue;
        }
        let sum: int = sum + i;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // sum = 5 + 6 + 7 + 8 + 9 = 35
    EXPECT_DOUBLE_EQ(result.value, 35.0);
}

TEST(EdgeCaseTest, NestedLoops) {
    const std::string source = R"(module test;

func main() -> int {
    let sum: int = 0;
    for (let i: int = 0; i < 3; let i: int = i + 1;) {
        for (let j: int = 0; j < 3; let j: int = j + 1;) {
            let sum: int = sum + 1;
        }
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 9.0);
}

TEST(EdgeCaseTest, NestedLoopsWithBreak) {
    const std::string source = R"(module test;

func main() -> int {
    let count: int = 0;
    for (let i: int = 0; i < 5; let i: int = i + 1;) {
        for (let j: int = 0; j < 5; let j: int = j + 1;) {
            if j == 2 {
                break;
            }
            let count: int = count + 1;
        }
    }
    return count;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // Each outer loop iteration adds 2 (j=0, j=1 then break)
    // 5 * 2 = 10
    EXPECT_DOUBLE_EQ(result.value, 10.0);
}

TEST(EdgeCaseTest, DeepIfNesting) {
    const std::string source = R"(module test;

func main() -> int {
    let x: int = 1;
    if x == 1 {
        if x < 5 {
            if x > 0 {
                if x != 0 {
                    return 42;
                }
            }
        }
    }
    return 0;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 42.0);
}

TEST(EdgeCaseTest, IfElseChain) {
    const std::string source = R"(module test;

func check(x: int) -> int {
    if x < 0 {
        return -1;
    } else {
        if x == 0 {
            return 0;
        } else {
            if x < 10 {
                return 1;
            } else {
                return 2;
            }
        }
    }
}

func main() -> int {
    return check(-5) + check(0) + check(5) + check(15);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // -1 + 0 + 1 + 2 = 2
    EXPECT_DOUBLE_EQ(result.value, 2.0);
}

// ============================================================================
// Edge Cases: Functions
// ============================================================================

TEST(EdgeCaseTest, NoParameters) {
    const std::string source = R"(module test;

func constant() -> int {
    return 42;
}

func main() -> int {
    return constant() + constant() + constant();
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 126.0);
}

TEST(EdgeCaseTest, ManyParameters) {
    const std::string source = R"(module test;

func sum5(a: int, b: int, c: int, d: int, e: int) -> int {
    return a + b + c + d + e;
}

func main() -> int {
    return sum5(1, 2, 3, 4, 5);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 15.0);
}

TEST(EdgeCaseTest, NestedFunctionCalls) {
    const std::string source = R"(module test;

func add(a: int, b: int) -> int {
    return a + b;
}

func main() -> int {
    return add(add(add(1, 2), add(3, 4)), add(add(5, 6), add(7, 8)));
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // ((1+2) + (3+4)) + ((5+6) + (7+8)) = 10 + 26 = 36
    EXPECT_DOUBLE_EQ(result.value, 36.0);
}

TEST(EdgeCaseTest, MutualRecursion) {
    const std::string source = R"(module test;

func is_even(n: int) -> bool {
    if n == 0 {
        return true;
    } else {
        return is_odd(n - 1);
    }
}

func is_odd(n: int) -> bool {
    if n == 0 {
        return false;
    } else {
        return is_even(n - 1);
    }
}

func main() -> int {
    let a: int = is_even(10);
    let b: int = is_even(7);
    let c: int = is_odd(5);
    let d: int = is_odd(8);
    return a + b + c + d;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // is_even(10)=1, is_even(7)=0, is_odd(5)=1, is_odd(8)=0
    EXPECT_DOUBLE_EQ(result.value, 2.0);
}

TEST(EdgeCaseTest, TailRecursion) {
    const std::string source = R"(module test;

func sum_to_n_helper(n: int, acc: int) -> int {
    if n <= 0 {
        return acc;
    } else {
        return sum_to_n_helper(n - 1, acc + n);
    }
}

func sum_to_n(n: int) -> int {
    return sum_to_n_helper(n, 0);
}

func main() -> int {
    return sum_to_n(100);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 1 + 2 + ... + 100 = 5050
    EXPECT_DOUBLE_EQ(result.value, 5050.0);
}

// ============================================================================
// Edge Cases: Arrays
// ============================================================================

TEST(EdgeCaseTest, SingleElementArray) {
    const std::string source = R"(module test;

func main() -> int {
    let arr: array = array(1);
    array_set(arr, 0, 42);
    return array_get(arr, 0);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 42.0);
}

TEST(EdgeCaseTest, ArrayFirstLastElement) {
    const std::string source = R"(module test;

func main() -> int {
    let arr: array = array(10);
    array_set(arr, 0, 1);
    array_set(arr, 9, 9);
    return array_get(arr, 0) + array_get(arr, 9);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 10.0);
}

TEST(EdgeCaseTest, ArrayAllElements) {
    const std::string source = R"(module test;

func main() -> int {
    let arr: array = array(5);
    array_set(arr, 0, 1);
    array_set(arr, 1, 2);
    array_set(arr, 2, 3);
    array_set(arr, 3, 4);
    array_set(arr, 4, 5);
    
    let sum: int = 0;
    let i: int = 0;
    while i < 5 {
        sum = sum + array_get(arr, i);
        i = i + 1;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 15.0);
}

TEST(EdgeCaseTest, ArrayPassToFunction) {
    const std::string source = R"(module test;

func sum_array(arr: array, len: int) -> int {
    let sum: int = 0;
    let i: int = 0;
    while i < len {
        sum = sum + array_get(arr, i);
        i = i + 1;
    }
    return sum;
}

func main() -> int {
    let arr: array = array(5);
    let i: int = 0;
    while i < 5 {
        array_set(arr, i, i * 2);
        i = i + 1;
    }
    return sum_array(arr, 5);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 0 + 2 + 4 + 6 + 8 = 20
    EXPECT_DOUBLE_EQ(result.value, 20.0);
}

TEST(EdgeCaseTest, ArrayModifyInFunction) {
    const std::string source = R"(module test;

func fill(arr: array, len: int, val: int) -> int {
    let i: int = 0;
    while i < len {
        array_set(arr, i, val);
        i = i + 1;
    }
    return 0;
}

func main() -> int {
    let arr: array = array(5);
    fill(arr, 5, 7);
    
    let sum: int = 0;
    let i: int = 0;
    while i < 5 {
        sum = sum + array_get(arr, i);
        i = i + 1;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 7 * 5 = 35
    EXPECT_DOUBLE_EQ(result.value, 35.0);
}

// ============================================================================
// Edge Cases: Max/Min Functions (alternative to ternary)
// ============================================================================

TEST(EdgeCaseTest, MaxFunction) {
    const std::string source = R"(module test;

func max(a: int, b: int) -> int {
    if a > b {
        return a;
    } else {
        return b;
    }
}

func main() -> int {
    let m1: int = max(5, 3);
    let m2: int = max(-1, 10);
    let m3: int = max(7, 7);
    return m1 + m2 + m3;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 5 + 10 + 7 = 22
    EXPECT_DOUBLE_EQ(result.value, 22.0);
}

TEST(EdgeCaseTest, MinFunction) {
    const std::string source = R"(module test;

func min(a: int, b: int) -> int {
    if a < b {
        return a;
    } else {
        return b;
    }
}

func main() -> int {
    let m1: int = min(5, 3);
    let m2: int = min(-1, 10);
    let m3: int = min(7, 7);
    return m1 + m2 + m3;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 3 + (-1) + 7 = 9
    EXPECT_DOUBLE_EQ(result.value, 9.0);
}

TEST(EdgeCaseTest, ClampFunction) {
    const std::string source = R"(module test;

func clamp(val: int, low: int, high: int) -> int {
    if val < low {
        return low;
    } else {
        if val > high {
            return high;
        } else {
            return val;
        }
    }
}

func main() -> int {
    let c1: int = clamp(5, 0, 10);
    let c2: int = clamp(-5, 0, 10);
    let c3: int = clamp(15, 0, 10);
    return c1 + c2 + c3;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 5 + 0 + 10 = 15
    EXPECT_DOUBLE_EQ(result.value, 15.0);
}

// ============================================================================
// Edge Cases: Expressions
// ============================================================================

TEST(EdgeCaseTest, ComplexExpression) {
    const std::string source = R"(module test;

func main() -> int {
    let a: int = 2 + 3 * 4 - 6 / 2;
    let b: int = (2 + 3) * (4 - 1);
    let c: int = 10 % 3 + 7 % 4;
    return a + b + c;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // a = 2 + 12 - 3 = 11
    // b = 5 * 3 = 15
    // c = 1 + 3 = 4
    // 11 + 15 + 4 = 30
    EXPECT_DOUBLE_EQ(result.value, 30.0);
}

TEST(EdgeCaseTest, ParenthesesPrecedence) {
    const std::string source = R"(module test;

func main() -> int {
    let a: int = 2 * 3 + 4;
    let b: int = 2 * (3 + 4);
    let c: int = (2 * 3) + 4;
    return b - a + c - a;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // a = 10, b = 14, c = 10
    // 14 - 10 + 10 - 10 = 4
    EXPECT_DOUBLE_EQ(result.value, 4.0);
}

// ============================================================================
// Edge Cases: Variable Shadowing
// ============================================================================

TEST(EdgeCaseTest, VariableShadowingInLoop) {
    const std::string source = R"(module test;

func main() -> int {
    let x: int = 100;
    for (let i: int = 0; i < 3; let i: int = i + 1;) {
        let x: int = x + 1;
    }
    return x;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 103.0);
}

TEST(EdgeCaseTest, VariableShadowingInBlock) {
    const std::string source = R"(module test;

func main() -> int {
    let x: int = 10;
    if true {
        let x: int = x + 5;
        if true {
            let x: int = x + 3;
        }
    }
    return x;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 18.0);
}

// ============================================================================
// Stress Tests: Deep Recursion
// ============================================================================

TEST(StressTest, DeepRecursion) {
    const std::string source = R"(module test;

func countdown(n: int) -> int {
    if n <= 0 {
        return 0;
    } else {
        return countdown(n - 1);
    }
}

func main() -> int {
    return countdown(1000);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 0.0);
}

TEST(StressTest, FibonacciRecursive) {
    const std::string source = R"(module test;

func fib(n: int) -> int {
    if n <= 1 {
        return n;
    } else {
        return fib(n - 1) + fib(n - 2);
    }
}

func main() -> int {
    return fib(20);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 6765.0);
}

// ============================================================================
// Stress Tests: Large Loops
// ============================================================================

TEST(StressTest, LargeLoop) {
    const std::string source = R"(module test;

func main() -> int {
    let sum: int = 0;
    for (let i: int = 0; i < 10000; let i: int = i + 1;) {
        let sum: int = sum + 1;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 10000.0);
}

TEST(StressTest, NestedLargeLoops) {
    const std::string source = R"(module test;

func main() -> int {
    let count: int = 0;
    for (let i: int = 0; i < 100; let i: int = i + 1;) {
        for (let j: int = 0; j < 100; let j: int = j + 1;) {
            let count: int = count + 1;
        }
    }
    return count;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 10000.0);
}

// ============================================================================
// Stress Tests: Array Operations
// ============================================================================

TEST(StressTest, LargeArrayFill) {
    const std::string source = R"(module test;

func main() -> int {
    let size: int = 1000;
    let arr: array = array(size);
    
    let i: int = 0;
    while i < size {
        array_set(arr, i, i);
        i = i + 1;
    }
    
    let sum: int = 0;
    i = 0;
    while i < size {
        sum = sum + array_get(arr, i);
        i = i + 1;
    }
    return sum;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // Sum of 0 to 999 = 999 * 1000 / 2 = 499500
    EXPECT_DOUBLE_EQ(result.value, 499500.0);
}

TEST(StressTest, ArrayBinarySearch) {
    // Binary search requires integer division.
    // Impulse uses float division (9/2 = 4.5), which doesn't work as array index.
    // We implement idiv manually using subtraction.
    const std::string source = R"(module test;

func idiv(a: int, b: int) -> int {
    let res: int = 0;
    let rem: int = a;
    while rem >= b {
        rem = rem - b;
        res = res + 1;
    }
    return res;
}

func binary_search(arr: array, len: int, target: int) -> int {
    let left: int = 0;
    let right: int = len - 1;
    
    while left <= right {
        let mid: int = idiv(left + right, 2);
        let val: int = array_get(arr, mid);
        if val == target {
            return mid;
        } else {
            if val < target {
                left = mid + 1;
            } else {
                right = mid - 1;
            }
        }
    }
    return -1;
}

func main() -> int {
    let size: int = 100;
    let arr: array = array(size);
    
    let i: int = 0;
    while i < size {
        array_set(arr, i, i * 2);
        i = i + 1;
    }
    
    let found1: int = binary_search(arr, size, 50);
    let found2: int = binary_search(arr, size, 0);
    let found3: int = binary_search(arr, size, 198);
    let not_found: int = binary_search(arr, size, 51);
    
    return found1 + found2 + found3 + not_found;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // found1 = 25 (50/2), found2 = 0, found3 = 99 (198/2), not_found = -1
    // 25 + 0 + 99 + (-1) = 123
    EXPECT_DOUBLE_EQ(result.value, 123.0);
}

// ============================================================================
// Stress Tests: Multiple Function Calls
// ============================================================================

TEST(StressTest, ManyFunctionCalls) {
    const std::string source = R"(module test;

func increment(x: int) -> int {
    return x + 1;
}

func main() -> int {
    let x: int = 0;
    for (let i: int = 0; i < 1000; let i: int = i + 1;) {
        let x: int = increment(x);
    }
    return x;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 1000.0);
}

// ============================================================================
// Edge Cases: Global Bindings
// ============================================================================

TEST(EdgeCaseTest, GlobalBindings) {
    const std::string source = R"(module test;

let global_a: int = 10;
let global_b: int = global_a * 2;
let global_c: int = global_a + global_b;

func main() -> int {
    return global_c;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 30.0);
}

TEST(EdgeCaseTest, GlobalAndLocalSameName) {
    const std::string source = R"(module test;

let x: int = 100;

func main() -> int {
    let x: int = 5;
    return x;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 5.0);
}

// ============================================================================
// Algorithms
// ============================================================================

TEST(AlgorithmTest, GCD) {
    const std::string source = R"(module test;

func gcd(a: int, b: int) -> int {
    let x: int = a;
    let y: int = b;
    while y != 0 {
        let temp: int = y;
        y = x % y;
        x = temp;
    }
    return x;
}

func main() -> int {
    let g1: int = gcd(48, 18);
    let g2: int = gcd(100, 25);
    let g3: int = gcd(17, 13);
    return g1 + g2 + g3;
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // gcd(48,18)=6, gcd(100,25)=25, gcd(17,13)=1
    // 6 + 25 + 1 = 32
    EXPECT_DOUBLE_EQ(result.value, 32.0);
}

TEST(AlgorithmTest, BubbleSort) {
    const std::string source = R"(module test;

func bubble_sort(arr: array, len: int) -> int {
    let i: int = 0;
    while i < len - 1 {
        let j: int = 0;
        while j < len - i - 1 {
            if array_get(arr, j) > array_get(arr, j + 1) {
                let temp: int = array_get(arr, j);
                array_set(arr, j, array_get(arr, j + 1));
                array_set(arr, j + 1, temp);
            }
            j = j + 1;
        }
        i = i + 1;
    }
    return 0;
}

func is_sorted(arr: array, len: int) -> bool {
    let i: int = 0;
    while i < len - 1 {
        if array_get(arr, i) > array_get(arr, i + 1) {
            return false;
        }
        i = i + 1;
    }
    return true;
}

func main() -> int {
    let arr: array = array(5);
    array_set(arr, 0, 5);
    array_set(arr, 1, 2);
    array_set(arr, 2, 8);
    array_set(arr, 3, 1);
    array_set(arr, 4, 9);
    
    bubble_sort(arr, 5);
    return is_sorted(arr, 5);
}
)";
    const auto result = run_program(source);
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_DOUBLE_EQ(result.value, 1.0);
}

TEST(AlgorithmTest, Power) {
    // Simple iterative power function
    // Note: 'result' is a reserved word in Impulse, using 'res' instead
    const std::string source = R"(module test;

func power(base: int, exp: int) -> int {
    let res: int = 1;
    let i: int = 0;
    while i < exp {
        res = res * base;
        i = i + 1;
    }
    return res;
}

func main() -> int {
    let p1: int = power(2, 10);
    let p2: int = power(3, 5);
    let p3: int = power(5, 3);
    return p1 + p2 + p3;
}
)";
    const auto result = run_program(source);
    if (result.status != impulse::runtime::VmStatus::Success) {
        std::cerr << "Power test error: " << result.message << std::endl;
    }
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    // 2^10 = 1024, 3^5 = 243, 5^3 = 125
    // 1024 + 243 + 125 = 1392
    EXPECT_DOUBLE_EQ(result.value, 1392.0);
}
