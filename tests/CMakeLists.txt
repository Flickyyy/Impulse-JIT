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
)

target_link_libraries(impulse-tests PRIVATE impulse-frontend impulse-ir impulse-runtime)

add_test(NAME impulse-tests COMMAND impulse-tests)
