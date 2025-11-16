add_executable(impulse-tests
	main.cpp
)

target_link_libraries(impulse-tests PRIVATE impulse-frontend impulse-ir impulse-runtime)

add_test(NAME impulse-tests COMMAND impulse-tests)
