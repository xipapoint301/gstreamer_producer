cmake_minimum_required(VERSION 3.16)
project(cam_stream)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
find_package(Threads REQUIRED)

add_executable(camera_node src/camera_pipeline.cpp src/main.cpp)
target_include_directories(camera_node PRIVATE include)
target_link_libraries(camera_node PkgConfig::GST Threads::Threads)

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
