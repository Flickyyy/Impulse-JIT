add_library(impulse-jit
    src/jit.cpp
)

target_include_directories(impulse-jit PUBLIC include)
target_link_libraries(impulse-jit PUBLIC impulse-ir)
target_compile_features(impulse-jit PUBLIC cxx_std_17)
