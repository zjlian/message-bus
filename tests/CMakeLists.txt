include_directories(${PROJECT_SOURCE_DIR})

add_executable(
    test_bus_server
    test_bus_server.cpp
)

target_link_libraries(
    test_bus_server PUBLIC
    ${PROJECT_NAME}
)

target_compile_options(
    test_bus_server PUBLIC
    -std=c++17
)

add_executable(
    test_pub
    test_pub.cpp
)

target_link_libraries(
    test_pub PUBLIC
    ${PROJECT_NAME}
)

target_compile_options(
    test_pub PUBLIC
    -std=c++17
)

add_executable(
    test_sub
    test_sub.cpp
)

target_compile_options(
    test_sub PUBLIC
    -std=c++17
)

if(${CMAKE_BUILD_TYPE})
    target_compile_definitions(test_sub PRIVATE BUILDTYPE=${CMAKE_BUILD_TYPE})
endif()

target_link_libraries(
    test_sub PUBLIC
    ${PROJECT_NAME}
)

add_executable(
    test_subt
    test_subt.cpp
)

target_link_libraries(
    test_subt PUBLIC
    ${PROJECT_NAME}
)


target_compile_options(
    test_subt PUBLIC
    -std=c++17
)

add_executable(
    log_
    log_.cpp
)

target_link_libraries(
    log_ PUBLIC
    ${PROJECT_NAME}
)

target_compile_options(
    log_ PUBLIC
    -std=c++17
)

add_executable(
    test_async_repreq
    test_async_repreq.cpp
)

target_link_libraries(
    test_async_repreq PUBLIC
    ${PROJECT_NAME}
)

target_compile_options(
    test_async_repreq PUBLIC
    -std=c++17
)

