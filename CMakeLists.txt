cmake_minimum_required(VERSION 3.0.0)
project(message_bus VERSION 0.1.0)
message(STATUS "message-bus project name: ${PROJECT_NAME}")
message(STATUS "message-bus project name: ${CMAKE_PROJECT_NAME}")

set(CMAKE_EXPORT_COMPILE_COMMANDS on)

# "编译模式 Debug/Release"
set(CMAKE_BUILD_TYPE Debug)
option(ENABLE_TESTS "是否编译单元测试" OFF)

set(SERVER_NODE_NAME "message-bus-server")

# 区分调试模式和发布模式的编译参数
if (CMAKE_BUILD_TYPE MATCHES Debug)
	message(STATUS "Debug 模式")
	add_definitions(
		-g
		-ggdb
		-O0
		-DDEBUG	# 定义宏 DEBUG
	)

elseif(CMAKE_BUILD_TYPE MATCHES Release)
	message(STATUS "Release 模式")
	add_definitions(
		-O3
		-DNDEBUG # 定义宏 NDEBUG，关闭 assert() 函数
	)
endif()

## 查找依赖
find_package(ZeroMQ REQUIRED)
find_package(Protobuf REQUIRED)
message(STATUS "找到 protobuf 头文件目录: ${Protobuf_FOUND}")
message(STATUS "找到 protobuf 头文件目录: ${PROTOBUF_INCLUDE_DIR}")
message(STATUS "找到 protobuf 需要链接的库: ${PROTOBUF_LIBRARY}")
find_package(cppzmq REQUIRED)
find_package(gflags REQUIRED)
message(STATUS "gflags include ${gflags_INCLUDE_DIR}")
message(STATUS "gflags libraries ${gflags_LIBRARIES}")

include(${PROJECT_SOURCE_DIR}/cmake-utility/compile_proto.cmake)
compile_proto(general_proto ${PROJECT_SOURCE_DIR}/general-proto)

set(PROJECT_INCLUDE_DIR "")
list(
	APPEND PROJECT_INCLUDE_DIR 
	${PROJECT_SOURCE_DIR}
    ${general_proto_INCLUDES}
    ${ZeroMQ_INCLUDE_DIR}
    ${cppzmq_INCLUDE_DIR}
)

set(PROJECT_LINK_LIBRARIES "")
list(
	APPEND PROJECT_LINK_LIBRARIES
	${general_proto_LIBRARIES}
    ${ZeroMQ_STATIC_LIBRARY}
    pthread
)

## 设置编译目标
add_subdirectory(message_bus)
add_subdirectory(rpc)

if (ENABLE_TESTS)
  message(STATUS "启用单元测试")
  add_subdirectory(tests)
endif()

add_executable(${SERVER_NODE_NAME} server.cpp)

## 设置编译依赖关系
target_include_directories(
	${SERVER_NODE_NAME} PRIVATE
	${gflags_INCLUDE_DIR}
)

target_link_libraries(
	${SERVER_NODE_NAME} PRIVATE
	${PROJECT_NAME}
	mbus_rpc
	${gflags_LIBRARIES}
)

target_compile_options(
	${SERVER_NODE_NAME} PRIVATE
	-std=c++17
)

if (CMAKE_BUILD_TYPE MATCHES Debug)
	target_link_options(
		${SERVER_NODE_NAME} PRIVATE
		-fsanitize=address
	)
	target_compile_options(
		${SERVER_NODE_NAME} PRIVATE
		-fsanitize=address 
		-fno-omit-frame-pointer 
		-fno-sanitize-recover
	)
endif()