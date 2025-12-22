add_library(impulse-frontend STATIC
	src/lexer.cpp
	src/parser.cpp
	src/lowering.cpp
	src/expression_eval.cpp
	src/semantic.cpp
	src/dump.cpp
)

target_include_directories(impulse-frontend PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_SOURCE_DIR}/runtime/include
)

target_link_libraries(impulse-frontend PUBLIC impulse-ir impulse-runtime)
