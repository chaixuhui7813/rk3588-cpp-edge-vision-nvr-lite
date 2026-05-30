cmake_minimum_required(VERSION 3.10)
project(rk3588_cpp_edge_vision_nvr_lite LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV QUIET)
if(NOT OpenCV_FOUND)
    message(FATAL_ERROR
        "OpenCV development package not found. Install libopencv-dev/pkg-config on Debian, "
        "or set OpenCV_DIR to the directory containing OpenCVConfig.cmake.")
endif()
find_package(Threads REQUIRED)

set(RKNN_INCLUDE_DIR "" CACHE PATH "Directory containing rknn_api.h")
set(RKNN_LIBRARY "" CACHE FILEPATH "Path to librknnrt.so")

find_path(RKNN_INCLUDE_DIR_AUTO rknn_api.h
    PATHS
        ${RKNN_INCLUDE_DIR}
        /usr/include
        /usr/local/include
        /usr/include/rknn
        /usr/local/include/rknn
        /usr/include/rknpu2
        /usr/local/include/rknpu2
        /opt/rknn/include
        /opt/rknpu2/include
)

find_library(RKNN_LIBRARY_AUTO rknnrt
    PATHS
        ${RKNN_LIBRARY}
        /usr/lib
        /usr/local/lib
        /usr/lib/aarch64-linux-gnu
        /usr/local/lib/aarch64-linux-gnu
        /opt/rknn/lib
        /opt/rknpu2/lib
)

if(RKNN_INCLUDE_DIR AND EXISTS "${RKNN_INCLUDE_DIR}/rknn_api.h")
    set(RKNN_INCLUDE_DIR_AUTO "${RKNN_INCLUDE_DIR}")
endif()

if(RKNN_LIBRARY AND EXISTS "${RKNN_LIBRARY}")
    set(RKNN_LIBRARY_AUTO "${RKNN_LIBRARY}")
endif()

set(SOURCES
    src/main.cpp
    src/config.cpp
    src/detector.cpp
    src/rknn_detector.cpp
    src/mock_detector.cpp
    src/yolo_postprocess.cpp
    src/video_pipeline.cpp
    src/event_manager.cpp
    src/system_stats.cpp
    src/mjpeg_server.cpp
    src/mqtt_client.cpp
    src/utils.cpp
)

add_executable(edge_vision_nvr ${SOURCES})

target_include_directories(edge_vision_nvr PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/httplib
    ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(edge_vision_nvr PRIVATE
    ${OpenCV_LIBS}
    Threads::Threads
)

if(RKNN_INCLUDE_DIR_AUTO AND RKNN_LIBRARY_AUTO)
    message(STATUS "RKNN Runtime found")
    message(STATUS "  rknn_api.h: ${RKNN_INCLUDE_DIR_AUTO}")
    message(STATUS "  librknnrt:  ${RKNN_LIBRARY_AUTO}")
    target_compile_definitions(edge_vision_nvr PRIVATE HAVE_RKNN=1)
    target_include_directories(edge_vision_nvr PRIVATE ${RKNN_INCLUDE_DIR_AUTO})
    target_link_libraries(edge_vision_nvr PRIVATE ${RKNN_LIBRARY_AUTO})
else()
    message(WARNING "RKNN Runtime not found. Building mock-capable binary without real RKNN support.")
    message(WARNING "Provide -DRKNN_INCLUDE_DIR=/path/to/include -DRKNN_LIBRARY=/path/to/librknnrt.so if needed.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(edge_vision_nvr PRIVATE -Wall -Wextra -Wpedantic)
endif()
