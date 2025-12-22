#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

#include "../frontend/include/impulse/frontend/lowering.h"
#include "../frontend/include/impulse/frontend/parser.h"
#include "../frontend/include/impulse/frontend/semantic.h"
#include "../runtime/include/impulse/runtime/gc_heap.h"
#include "../runtime/include/impulse/runtime/runtime.h"
#include "../runtime/include/impulse/runtime/value.h"

using impulse::runtime::GcHeap;
using impulse::runtime::GcObject;
using impulse::runtime::Value;

TEST(RuntimeTest, CollectsUnreachableObjects) {
    GcHeap heap;

    GcObject* object = heap.allocate_array(4);
    object->fields[0] = Value::make_number(42.0);

    Value root = Value::make_object(object);
    std::vector<Value*> roots = {&root};
    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 1);
    EXPECT_GT(heap.bytes_allocated(), 0);

    root = Value::make_nil();
    roots.clear();
    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 0);
    EXPECT_EQ(heap.bytes_allocated(), 0);
}

TEST(RuntimeTest, MarksTransitiveReferences) {
    GcHeap heap;

    GcObject* parent = heap.allocate_array(1);
    GcObject* child = heap.allocate_array(1);

    parent->fields[0] = Value::make_object(child);

    Value root = Value::make_object(parent);
    std::vector<Value*> roots = {&root};
    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 2);

    root = Value::make_nil();
    roots.clear();
    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 0);
}

TEST(RuntimeTest, CollectHandlesEmptyRoots) {
    GcHeap heap;
    std::vector<Value*> roots;
    heap.collect(roots);
    EXPECT_EQ(heap.live_object_count(), 0);
}

TEST(RuntimeTest, ArrayBuiltinsExecution) {
    const std::string source = R"(module demo;

func main() -> float {
    let values: array = array(4);
    array_set(values, 0, 10);
    array_set(values, 1, 20);
    array_set(values, 2, 30);
    array_set(values, 3, array_length(values));
    return array_get(values, 0) + array_get(values, 1) + array_get(values, 3);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    if (!semantic.success) {
        for (const auto& diagnostic : semantic.diagnostics) {
            std::fprintf(stderr, "semantic error: %s\n", diagnostic.message.c_str());
        }
    }
    ASSERT_TRUE(semantic.success) << "semantic analysis failed";

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 34.0), 1e-9);
}

TEST(RuntimeTest, ArrayRuntimeErrors) {
    const std::string source = R"(module demo;

func main() -> float {
    let values: array = array(2);
    return array_get(values, 5);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_TRUE(semantic.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::RuntimeError);
    EXPECT_FALSE(result.has_value);
    EXPECT_NE(result.message.find("array_get index out of bounds"), std::string::npos);
}

TEST(RuntimeTest, StringBuiltinsExecution) {
    const std::string source = R"(module demo;

func main() -> int {
    let pattern: string = "na";
    let chorus: string = string_repeat(pattern, 4);
    if string_equals(chorus, "nananana") {
        println(string_concat(chorus, " Batman"));
        return string_length(chorus);
    }
    return -1;
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    if (!semantic.success) {
        for (const auto& diagnostic : semantic.diagnostics) {
            std::fprintf(stderr, "semantic error: %s\n", diagnostic.message.c_str());
        }
    }
    ASSERT_TRUE(semantic.success) << "semantic analysis failed";

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 8.0), 1e-9);
}

TEST(RuntimeTest, StringSliceAndTransforms) {
    const std::string source = R"(module demo;

func main() -> int {
    let original: string = "  Impulse ";
    let trimmed: string = string_trim(original);
    if !string_equals(trimmed, "Impulse") {
        return -1;
    }
    let lower: string = string_lower(trimmed);
    let upper: string = string_upper(trimmed);
    if !string_equals(lower, "impulse") {
        return -2;
    }
    let slice: string = string_slice(upper, 1, 3);
    if !string_equals(slice, "MPU") {
        return -3;
    }
    return string_length(slice) + string_length(lower);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_TRUE(semantic.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 10.0), 1e-9);
}

TEST(RuntimeTest, ArrayStackBuiltins) {
    const std::string source = R"(module demo;

func main() -> int {
    let items: array = array(0);
    array_push(items, "a");
    array_push(items, "b");
    array_push(items, "c");
    let joined: string = array_join(items, "-");
    let popped: string = array_pop(items);
    if !string_equals(popped, "c") {
        return -1;
    }
    array_push(items, string_upper(popped));
    let final: string = array_join(items, ":");
    println(final);
    if !string_equals(joined, "a-b-c") {
        return -2;
    }
    if !string_equals(final, "a:b:C") {
        return -3;
    }
    return string_length(joined);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_TRUE(semantic.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 5.0), 1e-9);
}

TEST(RuntimeTest, ReadLineBuiltin) {
    const std::string source = R"(module demo;

func main() -> int {
    let value: string = read_line();
    return string_length(value);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_TRUE(semantic.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value), 1e-9);
}

TEST(RuntimeTest, ReadLineFromStream) {
    const std::string source = R"(module demo;

func main() -> int {
    let value: string = read_line();
    return string_length(value);
}
)";

    impulse::frontend::Parser parser(source);
    impulse::frontend::ParseResult parseResult = parser.parseModule();
    ASSERT_TRUE(parseResult.success);

    const auto semantic = impulse::frontend::analyzeModule(parseResult.module);
    EXPECT_TRUE(semantic.success);

    const auto lowered = impulse::frontend::lower_to_ir(parseResult.module);

    impulse::runtime::Vm vm;
    std::istringstream input("Impulse!\n");
    vm.set_input_stream(&input);
    const auto loadResult = vm.load(lowered);
    ASSERT_TRUE(loadResult.success);

    const auto result = vm.run("demo", "main");
    EXPECT_EQ(result.status, impulse::runtime::VmStatus::Success);
    EXPECT_TRUE(result.has_value);
    EXPECT_LT(std::abs(result.value - 8.0), 1e-9);
}

TEST(RuntimeTest, GcPreservesRoots) {
    GcHeap heap;
    heap.set_next_gc_threshold(1);

    GcObject* array = heap.allocate_array(4);
    Value root = Value::make_object(array);
    std::vector<Value*> roots = {&root};

    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 1);
    EXPECT_GE(heap.bytes_allocated(), sizeof(GcObject));
}

TEST(RuntimeTest, GcCollectsWhenThresholdExceeded) {
    GcHeap heap;
    heap.set_next_gc_threshold(64);

    for (int i = 0; i < 32; ++i) {
        [[maybe_unused]] GcObject* object = heap.allocate_array(8);
    }

    EXPECT_TRUE(heap.should_collect());

    std::vector<Value*> roots;
    heap.collect(roots);

    EXPECT_EQ(heap.live_object_count(), 0);
    EXPECT_EQ(heap.bytes_allocated(), 0);
}
