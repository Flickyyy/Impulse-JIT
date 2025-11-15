add_library(impulse-frontend STATIC
	src/lexer.cpp
	src/parser.cpp
	src/frontend.cpp
	src/lowering.cpp
	src/semantic.cpp
)

target_include_directories(impulse-frontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(impulse-frontend PUBLIC impulse-ir)
