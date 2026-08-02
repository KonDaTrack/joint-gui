cmake_minimum_required(VERSION 3.16)
project(joint_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# 目标架构（默认取宿主；交叉编译时由 toolchain 或 -DJOINT_ARCH 决定）
if(NOT DEFINED JOINT_ARCH)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
        set(JOINT_ARCH aarch64)
    else()
        set(JOINT_ARCH x86_64)
    endif()
endif()
message(STATUS "joint_gui target arch: ${JOINT_ARCH}")

find_package(Qt5 REQUIRED COMPONENTS Widgets)

add_executable(joint_gui
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
)
target_link_libraries(joint_gui PRIVATE Qt5::Widgets)
