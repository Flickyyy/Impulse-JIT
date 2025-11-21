add_library(impulse-ir STATIC
	src/printer.cpp
	src/builder.cpp
	src/interpreter.cpp
	src/cfg.cpp
	src/ssa.cpp
	src/ssa_verify.cpp
)

target_include_directories(impulse-ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
