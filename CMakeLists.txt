cmake_minimum_required(VERSION 3.24)

project(USBRestorationTool
    VERSION 0.1.0
    DESCRIPTION "Windows USB restore utility"
    LANGUAGES CXX RC
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc @ONLY)

set(APP_SOURCES
    src/main.cpp
    src/gui/MainWindow.cpp
    src/win/Admin.cpp
    src/win/Core.cpp
    src/win/DiskEnumerator.cpp
    src/win/Logger.cpp
    src/win/RawDisk.cpp
    src/win/RestoreWorker.cpp
    src/win/VolumeManager.cpp
    src/win/Wmi.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/app.rc
)

set(APP_HEADERS
    src/gui/MainWindow.h
    src/win/Admin.h
    src/win/Core.h
    src/win/DiskEnumerator.h
    src/win/Logger.h
    src/win/RawDisk.h
    src/win/RestoreWorker.h
    src/win/VolumeManager.h
    src/win/Wmi.h
)

add_executable(USBRestorationTool WIN32 ${APP_SOURCES} ${APP_HEADERS})
target_compile_definitions(USBRestorationTool PRIVATE UNICODE _UNICODE NOMINMAX)
target_include_directories(USBRestorationTool PRIVATE src)
target_link_libraries(USBRestorationTool PRIVATE
    Qt6::Widgets
    ole32
    oleaut32
    wbemuuid
    shell32
    advapi32
    uuid
)
target_link_options(USBRestorationTool PRIVATE "/MANIFEST:NO")

set_target_properties(USBRestorationTool PROPERTIES
    OUTPUT_NAME "USBRestorationTool"
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:USBRestorationTool>"
)

enable_testing()

add_executable(USBRestorationToolTests
    tests/CoreTests.cpp
    src/win/Core.cpp
)
target_compile_definitions(USBRestorationToolTests PRIVATE UNICODE _UNICODE NOMINMAX)
target_include_directories(USBRestorationToolTests PRIVATE src)
target_link_libraries(USBRestorationToolTests PRIVATE Qt6::Test)
add_test(NAME USBRestorationToolTests COMMAND USBRestorationToolTests)
