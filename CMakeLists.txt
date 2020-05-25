############# settings
project(tcp)
cmake_minimum_required(VERSION 3.4)

SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pthread -g -ggdb -Wall -DDEBUG -I${INC_DIR}")
add_definitions(-DSLIDING_WINDOW)


############## files
set(TOP_DIR .)
set(INC_DIR ${TOP_DIR}/inc)
set(SRC_DIR ${TOP_DIR}/src)
set(BUILD_DIR ${TOP_DIR}/build)
set(TEST_DIR ${TOP_DIR}/test)

file(GLOB backend_srcs
        ${PROJECT_SOURCE_DIR}/src/backend.c
        ${PROJECT_SOURCE_DIR}/src/cmu_packet.c
        ${PROJECT_SOURCE_DIR}/src/cmu_tcp.c
        ${PROJECT_SOURCE_DIR}/src/handshake.c
        )


############# executables
include_directories(${PROJECT_SOURCE_DIR}/src/
        ${PROJECT_SOURCE_DIR}/inc/
        ${PROJECT_SOURCE_DIR}/test/
        ${PROJECT_SOURCE_DIR}/utils/)

add_library(backend_lib ${backend_srcs}) #file/string is variable, lib is not

add_executable(server
        ${PROJECT_SOURCE_DIR}/src/server.c
        )
target_link_libraries(server PUBLIC
        backend_lib)

add_executable(client
        ${PROJECT_SOURCE_DIR}/src/client.c
        )
target_link_libraries(client PUBLIC
        backend_lib)

add_executable(testing_server
        ${PROJECT_SOURCE_DIR}/test/testing_server.c
        )
target_link_libraries(testing_server PUBLIC
        backend_lib
        )

