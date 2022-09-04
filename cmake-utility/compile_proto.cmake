#
# @brief 编译指定目录下的 proto 文件
# @param OUTPUT_NAME 指定编译结果的自定义名称
# @param PROTO_DIR 指定需要编译的 proto 目录
#
# 宏 compile_proto 执行后，会定义两个变量，用于后续链接库文件和引入头文件，
# 输出的变量是 <OUTPUT_NAME>_INCLUDES <OUTPUT_NAME>_LIBRARIES
# 用法示例:
# compile_proto(my_proto proto)
# target_include_directories(main PUBLIC ${my_proto_INCLUDES})
# target_link_libraries(main PUBLIC ${my_proto_LIBRARIES})
#
macro(compile_proto OUTPUT_NAME PROTO_DIR)
    # file(GLOB PROTOS ${PROJECT_SOURCE_DIR}/proto/*.proto)
    file(GLOB PROTOS ${PROTO_DIR}/*.proto)
    set(PROTO_OUTPUT_DIR "${PROJECT_BINARY_DIR}/${OUTPUT_NAME}")
    set(PROTO_SRCS "")
    set(PROTO_HDRS "")
    execute_process(COMMAND mkdir -p ${PROTO_OUTPUT_DIR})
    message(STATUS "开始编译 proto 文件")
    foreach(file ${PROTOS})
        get_filename_component(NAME ${file} NAME_WE)
        list(APPEND PROTO_SRCS "${PROTO_OUTPUT_DIR}/${NAME}.pb.cc")
        list(APPEND PROTO_HDRS "${PROTO_OUTPUT_DIR}/${NAME}.pb.h")
        execute_process(
            COMMAND
            ${PROTOBUF_PROTOC_EXECUTABLE} -I=${PROTO_DIR} --cpp_out=${PROTO_OUTPUT_DIR} ${file}
        )
        message(STATUS "${PROTOBUF_PROTOC_EXECUTABLE} -I=${PROTO_DIR} --cpp_out=${PROTO_OUTPUT_DIR} ${file}")
    endforeach()

    # 将 proto c++ 源码编译成静态库
    add_library(
        ${OUTPUT_NAME} STATIC
        ${PROTO_SRCS}
    )
    target_include_directories(
        ${OUTPUT_NAME} PUBLIC
        ${PROTOBUF_INCLUDE_DIR}
    )
    target_link_libraries(
        ${OUTPUT_NAME} PUBLIC
        ${PROTOBUF_LIBRARY}
    )

    message(STATUS "Protobuf_INCLUDE_DIR ${Protobuf_INCLUDE_DIR}")
    message(STATUS "Protobuf_LIBRARIES ${Protobuf_LIBRARIES}")
    
    # 设置 proto 文件的 include 路径
    set(${OUTPUT_NAME}_INCLUDES ${PROTO_OUTPUT_DIR})
    # 设置 proto 文件编译后的库文件路径
    set(${OUTPUT_NAME}_LIBRARIES ${OUTPUT_NAME})
endmacro()

