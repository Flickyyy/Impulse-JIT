add_executable(impulse-cpp
    main.cpp
)

set_target_properties(impulse-cpp PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)

target_link_libraries(impulse-cpp PRIVATE impulse-frontend impulse-ir impulse-runtime)

target_include_directories(impulse-cpp PRIVATE
    ${CMAKE_SOURCE_DIR}/frontend/include
    ${CMAKE_SOURCE_DIR}/ir/include
    ${CMAKE_SOURCE_DIR}/runtime/include
)
