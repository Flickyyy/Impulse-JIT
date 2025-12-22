add_executable(impulse-tests
	main.cpp
	test_lexer.cpp
	test_parser.cpp
	test_semantic.cpp
	test_ir.cpp
	test_operator.cpp
	test_control_flow.cpp
	test_functions.cpp
	test_runtime.cpp
	test_acceptance.cpp
	test_benchmarks.cpp
	test_jit.cpp
	test_profiling.cpp
	test_sorting_profile.cpp
	acceptance/harness.cpp
)

target_link_libraries(impulse-tests PRIVATE 
	impulse-frontend 
	impulse-ir 
	impulse-runtime
	GTest::gtest
	GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(impulse-tests)
