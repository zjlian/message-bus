aux_source_directory(. SOURCES)
add_library(
    mbus_rpc STATIC
    ${SOURCES}
)

target_include_directories(
	mbus_rpc PUBLIC
	${PROJECT_INCLUDE_DIR}
)

target_link_libraries(
    mbus_rpc PUBLIC 
    ${PROJECT_LINK_LIBRARIES}
)

target_compile_options(
    mbus_rpc PRIVATE
    -std=c++17
)