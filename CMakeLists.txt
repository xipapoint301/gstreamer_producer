cmake_minimum_required(VERSION 3.16)
project(cam_stream)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GST REQUIRED IMPORTED_TARGET
    gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0)
find_package(Threads REQUIRED)
find_package(yaml-cpp REQUIRED)

add_executable(camera_node src/camera_pipeline.cpp src/main.cpp)
target_include_directories(camera_node PRIVATE include)

# yaml-cpp >=0.7 exports the yaml-cpp::yaml-cpp target; older releases (e.g. Ubuntu's
# libyaml-cpp-dev 0.6.x) only set YAML_CPP_LIBRARIES/YAML_CPP_INCLUDE_DIR.
if(TARGET yaml-cpp::yaml-cpp)
    set(YAML_CPP_TARGET yaml-cpp::yaml-cpp)
else()
    set(YAML_CPP_TARGET ${YAML_CPP_LIBRARIES})
    target_include_directories(camera_node PRIVATE ${YAML_CPP_INCLUDE_DIR})
endif()

target_link_libraries(camera_node PkgConfig::GST Threads::Threads ${YAML_CPP_TARGET})

install(TARGETS camera_node DESTINATION lib/${PROJECT_NAME})
install(DIRECTORY config DESTINATION share/${PROJECT_NAME})
