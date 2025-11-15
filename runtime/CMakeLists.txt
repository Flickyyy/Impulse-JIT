add_library(impulse-runtime STATIC
	src/placeholder.cpp
)

target_link_libraries(impulse-runtime PUBLIC impulse-ir)
