cmake_minimum_required(VERSION 3.16)
project(joint_gui LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
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

find_package(Qt5 REQUIRED COMPONENTS Widgets Network)

add_executable(joint_gui
    src/main.cpp
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/ConnectionDialog.cpp
    src/ui/ConnectionDialog.h
    src/ui/MonitorPanel.cpp
    src/ui/MonitorPanel.h
    src/ui/ControlPanel.cpp
    src/ui/ControlPanel.h
    src/device/JointTypes.cpp
    src/device/JointTypes.h
    src/core/AppConfig.cpp
    src/core/AppConfig.h
    src/core/ControlWorker.cpp
    src/core/ControlWorker.h
    src/device/JointDevice.h
    src/device/DeviceFactory.cpp
    src/device/DeviceFactory.h
    src/device/sim/SimulatedDevice.cpp
    src/device/sim/SimulatedDevice.h
    src/device/sim/JointModel.cpp
    src/device/sim/JointModel.h
)
# 统一按 src 相对路径包含头文件（"ui/..."、"core/..."、"device/..."）。
# 后续新增 target（如 unit_tests）必须复用此变量，否则找不到头文件。
set(JOINT_GUI_INCLUDE_DIRS src)
target_include_directories(joint_gui PRIVATE ${JOINT_GUI_INCLUDE_DIRS})
target_link_libraries(joint_gui PRIVATE Qt5::Widgets Qt5::Network)

find_package(Qt5 REQUIRED COMPONENTS Test)
enable_testing()

add_executable(unit_tests
    tests/TestMain.cpp
    tests/TestUnitConverter.cpp
    tests/TestJointModel.cpp
    tests/TestMitFrameCodec.cpp
    src/core/UnitConverter.cpp
    src/device/sim/JointModel.cpp
    src/device/JointTypes.cpp   # TestJointModel 依赖 Joint::mapDriveState
    src/device/canopen/MitFrameCodec.cpp
)
target_include_directories(unit_tests PRIVATE ${JOINT_GUI_INCLUDE_DIRS})
target_link_libraries(unit_tests PRIVATE Qt5::Test Qt5::Core)
add_test(NAME unit_tests COMMAND unit_tests)
