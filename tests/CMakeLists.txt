include(${PROJECT_SOURCE_DIR}/cmake-utility/create_test.cmake)

include_directories(${PROJECT_SOURCE_DIR})

link_libraries(
    ${PROJECT_NAME}
    mbus_rpc
)

add_compile_options(
    -std=c++17
)


create_test(test_bus_server test_bus_server.cpp)



create_test(
    test_pub
    test_pub.cpp
    add_service.pb.cc
)



create_test(
    test_sub
    test_sub.cpp
    add_service.pb.cc
)

create_test(
    test_subt
    test_subt.cpp
)

create_test(
    log_
    log_.cpp
)


create_test(
    test_async_repreq
    test_async_repreq.cpp
)


create_test(
    test_rpc_worker
    test_rpc_worker.cpp
    add_service.pb.cc
)

create_test(
    test_rpc_client
    test_rpc_client.cpp
    add_service.pb.cc
)
