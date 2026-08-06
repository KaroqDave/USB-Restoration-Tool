cmake_minimum_required(VERSION 3.21)

project(USBRestorationTool
    VERSION 1.0.0
    DESCRIPTION "Restore ISO-written USB drives to one clean exFAT volume"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR
        "USB Restoration Tool targets Windows only: it drives the Windows storage stack "
        "directly through DeviceIoControl and the MSFT_Storage WMI classes.")
endif()

enable_language(RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(USBRESTORE_BUILD_TESTS "Build the USB Restoration Tool C++ tests" ON)

# Fallback Qt discovery: when configuring without a preset or CMAKE_PREFIX_PATH
# (straight from an IDE, say), locate the newest Qt MSVC kit under C:/Qt so
# find_package(Qt6) works out of the box.
if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH AND NOT DEFINED ENV{CMAKE_PREFIX_PATH})
    file(GLOB _usbrestore_qt_configs "C:/Qt/*/msvc*_64/lib/cmake/Qt6/Qt6Config.cmake")
    if(_usbrestore_qt_configs)
        list(SORT _usbrestore_qt_configs COMPARE NATURAL)
        list(GET _usbrestore_qt_configs -1 _usbrestore_qt_config)
        get_filename_component(_usbrestore_qt_cmake_dir "${_usbrestore_qt_config}" DIRECTORY)
        get_filename_component(_usbrestore_qt_prefix "${_usbrestore_qt_cmake_dir}/../../.." ABSOLUTE)
        list(APPEND CMAKE_PREFIX_PATH "${_usbrestore_qt_prefix}")
        message(STATUS "Auto-detected Qt at ${_usbrestore_qt_prefix}")
    endif()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
if(USBRESTORE_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
endif()
get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)

# Pure policy and formatting, with no Windows headers behind it, so the safety
# rules that decide whether a disk may be erased are testable on their own.
set(USBRESTORE_CORE_SOURCES
    src/core/disk.cpp
    src/core/disk.h
    src/core/safety.cpp
    src/core/safety.h
)

set(USBRESTORE_WIN_SOURCES
    src/win/admin.cpp
    src/win/admin.h
    src/win/disk_enumerator.cpp
    src/win/disk_enumerator.h
    src/win/logger.cpp
    src/win/logger.h
    src/win/raw_disk.cpp
    src/win/raw_disk.h
    src/win/restore_worker.cpp
    src/win/restore_worker.h
    src/win/volume_manager.cpp
    src/win/volume_manager.h
    src/win/windows_util.cpp
    src/win/windows_util.h
    src/win/wmi.cpp
    src/win/wmi.h
)

set(USBRESTORE_GUI_SOURCES
    src/gui/app_settings.cpp
    src/gui/app_settings.h
    src/gui/main.cpp
    src/gui/main_window.cpp
    src/gui/main_window.h
    src/gui/theme.cpp
    src/gui/theme.h
    resources/resources.qrc
)

# The version resource is generated so PROJECT_VERSION stays the only place a
# release is bumped.
set(USBRESTORE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
set(USBRESTORE_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

add_executable(usb-restoration-tool WIN32
    ${USBRESTORE_CORE_SOURCES}
    ${USBRESTORE_WIN_SOURCES}
    ${USBRESTORE_GUI_SOURCES}
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
)

target_include_directories(usb-restoration-tool PRIVATE src)
target_compile_definitions(usb-restoration-tool PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    USBRESTORE_APP_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(usb-restoration-tool PRIVATE
    Qt6::Core
    Qt6::Widgets
    ole32
    oleaut32
    wbemuuid
    advapi32
    shell32
    uuid
)

# The manifest is embedded through the .rc file, so the linker must not add one
# of its own.
target_link_options(usb-restoration-tool PRIVATE "/MANIFEST:NO")

if(MSVC)
    # An elevated process is worth compiling defensively: /sdl promotes the
    # security-relevant warnings to errors, /guard:cf adds control-flow guard,
    # and /CETCOMPAT opts into hardware shadow stacks where the CPU has them.
    # /utf-8 so the check marks and separators in the UI strings are compiled as
    # the UTF-8 that QStringLiteral expects, rather than as whatever the build
    # machine's active code page happens to be.
    target_compile_options(usb-restoration-tool PRIVATE /utf-8 /W4 /permissive- /sdl /guard:cf)
    target_link_options(usb-restoration-tool PRIVATE /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
endif()

set_target_properties(usb-restoration-tool PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:usb-restoration-tool>"
)

find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_INSTALL_PREFIX}/bin")
if(WINDEPLOYQT_EXECUTABLE)
    add_custom_command(
        TARGET usb-restoration-tool
        POST_BUILD
        COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:usb-restoration-tool>"
        COMMENT "Deploying Qt runtime files"
    )
endif()

if(USBRESTORE_BUILD_TESTS)
    enable_testing()

    add_executable(usb-restoration-tool-tests
        ${USBRESTORE_CORE_SOURCES}
        tests/test_core.cpp
    )
    target_include_directories(usb-restoration-tool-tests PRIVATE src)
    target_compile_definitions(usb-restoration-tool-tests PRIVATE UNICODE _UNICODE NOMINMAX)
    if(MSVC)
        target_compile_options(usb-restoration-tool-tests PRIVATE /utf-8)
    endif()
    target_link_libraries(usb-restoration-tool-tests PRIVATE Qt6::Core Qt6::Test)

    add_test(NAME usb-restoration-tool-tests COMMAND usb-restoration-tool-tests)
    set_tests_properties(usb-restoration-tool-tests PROPERTIES
        ENVIRONMENT "PATH=${QT_INSTALL_PREFIX}/bin;$ENV{PATH}")
endif()

install(TARGETS usb-restoration-tool RUNTIME DESTINATION ".")
if(WINDEPLOYQT_EXECUTABLE)
    install(CODE "
        execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
            --no-translations --no-compiler-runtime
            \"\${CMAKE_INSTALL_PREFIX}/usb-restoration-tool.exe\")
    ")
endif()
