add_library(impulse-ir STATIC
	src/printer.cpp
	src/builder.cpp
	src/interpreter.cpp
)

target_include_directories(impulse-ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
