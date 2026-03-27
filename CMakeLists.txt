cmake_minimum_required(VERSION 3.16)
project(mini-cloud-db LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# io_uring 需要 liburing
find_library(URING_LIB uring)
if(NOT URING_LIB)
    message(WARNING "liburing not found, io_uring engine will not be available")
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)

# 收集所有源文件
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_library(miniclouddb STATIC ${SOURCES})
target_link_libraries(miniclouddb PUBLIC pthread)
if(URING_LIB)
    target_link_libraries(miniclouddb PUBLIC ${URING_LIB})
    target_compile_definitions(miniclouddb PUBLIC HAS_IO_URING=1)
endif()

# 测试
enable_testing()
file(GLOB TEST_SOURCES "test/*.cpp")
foreach(test_source ${TEST_SOURCES})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} miniclouddb)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
