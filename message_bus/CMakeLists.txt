## 编译 message-bus 的源码成静态库
aux_source_directory(./impl SOURCES)

add_library(
  ${PROJECT_NAME} STATIC
  ${SOURCES}
)

target_include_directories(
	${PROJECT_NAME} PUBLIC
	${PROJECT_INCLUDE_DIR}
)

target_link_libraries(
    ${PROJECT_NAME} PUBLIC 
    ${PROJECT_LINK_LIBRARIES}
)

target_compile_options(
  ${PROJECT_NAME} PRIVATE
  -std=c++17
)