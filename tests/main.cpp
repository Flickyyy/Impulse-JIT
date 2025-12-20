#include <gtest/gtest.h>
#include "test_groups.h"

// Wrapper tests for legacy test functions
TEST(SemanticTest, AllTests) { EXPECT_EQ(runSemanticTests(), 22); }
TEST(IRTest, AllTests) { EXPECT_EQ(runIRTests(), 12); }
TEST(OperatorTest, AllTests) { EXPECT_EQ(runOperatorTests(), 6); }
TEST(ControlFlowTest, AllTests) { EXPECT_EQ(runControlFlowTests(), 4); }
TEST(FunctionCallTest, AllTests) { EXPECT_EQ(runFunctionCallTests(), 2); }
TEST(RuntimeTest, AllTests) { EXPECT_EQ(runRuntimeTests(), 12); }
TEST(AcceptanceTest, AllTests) { EXPECT_EQ(runAcceptanceTests(), 13); }

auto main(int argc, char** argv) -> int {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
