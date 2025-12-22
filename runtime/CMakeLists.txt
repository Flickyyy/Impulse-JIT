add_library(impulse-runtime STATIC
	src/runtime.cpp
	src/runtime_utils.cpp
	src/ssa_interpreter.cpp
	src/gc_heap.cpp
)

target_include_directories(impulse-runtime PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(impulse-runtime PUBLIC impulse-ir)
