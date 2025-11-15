add_library(impulse-ir STATIC
	src/placeholder.cpp
	src/printer.cpp
	src/builder.cpp
)

target_include_directories(impulse-ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
