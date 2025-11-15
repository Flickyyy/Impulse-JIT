add_library(impulse-frontend STATIC
	src/lexer.cpp
	src/parser.cpp
	src/frontend.cpp
)

target_include_directories(impulse-frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
