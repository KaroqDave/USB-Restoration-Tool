cmake_minimum_required(VERSION 3.21)

project(USBRestorationTool
    VERSION 1.4.0
    DESCRIPTION "Restore ISO-written USB drives to one clean volume"
    LANGUAGES CXX
)

if(NOT WIN32 AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux"))
    message(FATAL_ERROR
        "USB Restoration Tool supports Windows and Linux only: each backend drives its "
        "platform's storage stack directly.")
endif()

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Ninja and the Makefile generators honour this; Visual Studio ignores it.
# usbrestore_export_clangd_compile_commands() writes a clang-style database
# next to this file so the language server still sees src/ and Qt.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(USBRESTORE_BUILD_TESTS "Build the USB Restoration Tool C++ tests" ON)

# On by default because a real install — .deb, .rpm, AUR — is where the polkit
# privilege split can actually work. The AppImage turns it off: polkit reads its
# policies from system directories, so a self-contained bundle cannot register
# one, and a helper it could never obtain privilege through is dead weight.
option(USBRESTORE_INSTALL_HELPER "Install the privileged Linux restore helper" ON)

# Fallback Qt discovery: when configuring without a preset or CMAKE_PREFIX_PATH
# (straight from an IDE, say), locate the newest C:/Qt kit that matches this
# compiler so find_package(Qt6) works out of the box. A MinGW kit cannot be
# linked by MSVC (GNU import libs), so the globs stay compiler-specific.
if(WIN32)
    # A previous failed configure caches Qt6_DIR as *-NOTFOUND, which would
    # skip this block and keep find_package from seeing a kit we just found.
    if(Qt6_DIR MATCHES "-NOTFOUND$")
        unset(Qt6_DIR CACHE)
    endif()
    if(NOT Qt6_DIR AND NOT CMAKE_PREFIX_PATH AND NOT DEFINED ENV{CMAKE_PREFIX_PATH})
        if(MSVC)
            set(_usbrestore_qt_kit_glob "C:/Qt/*/msvc*_64/lib/cmake/Qt6/Qt6Config.cmake")
            set(_usbrestore_qt_kit_name "MSVC 64-bit")
        else()
            set(_usbrestore_qt_kit_glob "C:/Qt/*/mingw*_64/lib/cmake/Qt6/Qt6Config.cmake")
            set(_usbrestore_qt_kit_name "MinGW 64-bit")
        endif()
        file(GLOB _usbrestore_qt_configs "${_usbrestore_qt_kit_glob}")
        if(_usbrestore_qt_configs)
            list(SORT _usbrestore_qt_configs COMPARE NATURAL)
            list(GET _usbrestore_qt_configs -1 _usbrestore_qt_config)
            get_filename_component(_usbrestore_qt_cmake_dir "${_usbrestore_qt_config}" DIRECTORY)
            get_filename_component(_usbrestore_qt_prefix "${_usbrestore_qt_cmake_dir}/../../.." ABSOLUTE)
            list(APPEND CMAKE_PREFIX_PATH "${_usbrestore_qt_prefix}")
            message(STATUS "Auto-detected Qt at ${_usbrestore_qt_prefix}")
        else()
            file(GLOB _usbrestore_other_qt "C:/Qt/*/*_64/lib/cmake/Qt6/Qt6Config.cmake")
            if(_usbrestore_other_qt)
                list(SORT _usbrestore_other_qt COMPARE NATURAL)
                list(GET _usbrestore_other_qt -1 _usbrestore_other_config)
                get_filename_component(_usbrestore_other_cmake_dir "${_usbrestore_other_config}" DIRECTORY)
                get_filename_component(_usbrestore_other_prefix "${_usbrestore_other_cmake_dir}/../../.." ABSOLUTE)
                message(FATAL_ERROR
                    "Qt was found at ${_usbrestore_other_prefix}, but that kit does not match this "
                    "compiler (need a ${_usbrestore_qt_kit_name} kit). Install that component with "
                    "Qt Maintenance Tool, or set CMAKE_PREFIX_PATH to it.")
            endif()
        endif()
    endif()
endif()

if(NOT WIN32)
    include(GNUInstallDirs)

    # The one place the helper's installed location is decided. It is compiled
    # into the GUI, written into the polkit action, and used as the install
    # destination — pkexec authorises one absolute path, so all three have to be
    # the same string or the prompt fails with nothing useful to say.
    set(USBRESTORE_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/usb-restoration-helper")
endif()

set(_usbrestore_qt_components Core Widgets)
if(USBRESTORE_BUILD_TESTS)
    list(APPEND _usbrestore_qt_components Test)
endif()
find_package(Qt6 REQUIRED COMPONENTS ${_usbrestore_qt_components})
unset(_usbrestore_qt_components)
get_filename_component(QT_INSTALL_PREFIX "${Qt6_DIR}/../../.." ABSOLUTE)

# Policy, formatting and partition-table bytes, with no OS headers behind any
# of it. This is the half that decides whether a disk may be erased and what
# gets written to it, and the half the tests cover.
set(USBRESTORE_CORE_SOURCES
    src/core/disk.cpp
    src/core/disk.h
    src/core/partition_table.cpp
    src/core/partition_table.h
    src/core/restore_protocol.cpp
    src/core/restore_protocol.h
    src/core/safety.cpp
    src/core/safety.h
)

# The platform-agnostic half of the platform layer: the interface each backend
# implements, the worker that drives one, and the log.
set(USBRESTORE_PLATFORM_SOURCES
    src/platform/disk_service.h
    src/platform/logger.cpp
    src/platform/logger.h
    src/platform/restore_worker.cpp
    src/platform/restore_worker.h
    src/platform/startup.h
)

set(USBRESTORE_WIN_SOURCES
    src/win/admin.cpp
    src/win/admin.h
    src/win/disk_enumerator.cpp
    src/win/disk_enumerator.h
    src/win/raw_disk.cpp
    src/win/raw_disk.h
    src/win/volume_manager.cpp
    src/win/volume_manager.h
    src/win/windows_disk_service.cpp
    src/win/windows_disk_service.h
    src/win/windows_util.cpp
    src/win/windows_util.h
    src/win/wmi.cpp
    src/win/wmi.h
)

# Everything a restore needs and nothing that presents one. This is the set the
# privileged helper links: no DiskService, no worker, no widgets.
set(USBRESTORE_LINUX_RESTORE_SOURCES
    src/linux/block_device.cpp
    src/linux/block_device.h
    src/linux/linux_enumerate.cpp
    src/linux/linux_enumerate.h
    src/linux/linux_restore.cpp
    src/linux/linux_restore.h
    src/linux/linux_startup.cpp
    src/linux/sysfs.cpp
    src/linux/sysfs.h
)

# The above plus the GUI-only half: the DiskService, and the client that starts
# the helper through pkexec. The helper must never link helper_client — nothing
# would go wrong, but a privileged binary that knows how to ask for privilege is
# a loop waiting to be written.
set(USBRESTORE_LINUX_SOURCES
    ${USBRESTORE_LINUX_RESTORE_SOURCES}
    src/linux/helper_client.cpp
    src/linux/helper_client.h
    src/linux/linux_disk_service.cpp
    src/linux/linux_disk_service.h
)

# Applied to every target that ends up holding privilege, which since the
# helper was split out is more than one of them. A flag list that lives in one
# place is a flag list the two binaries cannot drift apart on.
function(usbrestore_harden target)
    if(MSVC)
        # An elevated process is worth compiling defensively: /sdl promotes the
        # security-relevant warnings to errors, /guard:cf adds control-flow
        # guard, and /CETCOMPAT opts into hardware shadow stacks where the CPU
        # has them. /utf-8 so the check marks and separators in the UI strings
        # are compiled as the UTF-8 that QStringLiteral expects.
        target_compile_options(${target} PRIVATE /utf-8 /W4 /permissive- /sdl /guard:cf)
        target_link_options(${target} PRIVATE /guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        # The same intent as the MSVC flags above: a process running as root
        # should be built with the hardening its toolchain offers.
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -D_FORTIFY_SOURCE=2 -fstack-protector-strong -fPIE)
        target_link_options(${target} PRIVATE -pie -Wl,-z,relro,-z,now,-z,noexecstack)
    endif()
endfunction()

function(usbrestore_json_escape str outvar)
    string(REPLACE "\\" "\\\\" str "${str}")
    string(REPLACE "\"" "\\\"" str "${str}")
    set(${outvar} "${str}" PARENT_SCOPE)
endfunction()

# Clangd walks up from the open file looking for compile_commands.json. The
# Visual Studio generator never writes one, and Ninja writes it into the build
# tree, which clangd does not search. Either way the editor reports core/disk.h
# and every Qt header as missing. A clang-style database next to CMakeLists.txt
# (gitignored, machine-specific Qt paths) is what the language server actually
# reads; CMAKE_EXPORT_COMPILE_COMMANDS stays on for tools that look in the
# build tree.
function(usbrestore_export_clangd_compile_commands)
    set(_includes "${CMAKE_SOURCE_DIR}/src")
    foreach(_cfg IN ITEMS "" Release Debug RelWithDebInfo MinSizeRel)
        if(_cfg STREQUAL "")
            set(_suffix "")
        else()
            set(_suffix "_${_cfg}")
        endif()
        list(APPEND _includes
            "${CMAKE_BINARY_DIR}/usb-restoration-tool_autogen/include${_suffix}"
            "${CMAKE_BINARY_DIR}/usb-restoration-tool-tests_autogen/include${_suffix}"
            "${CMAKE_BINARY_DIR}/usb-partition-dump_autogen/include${_suffix}"
        )
    endforeach()

    foreach(_tgt IN ITEMS Qt6::Platform Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        if(NOT TARGET ${_tgt})
            continue()
        endif()
        get_target_property(_tgt_inc ${_tgt} INTERFACE_INCLUDE_DIRECTORIES)
        if(_tgt_inc)
            foreach(_p IN LISTS _tgt_inc)
                if(NOT _p MATCHES "\\$<")
                    list(APPEND _includes "${_p}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _includes)

    set(_defs
        QT_CORE_LIB
        QT_GUI_LIB
        QT_WIDGETS_LIB
        "USBRESTORE_APP_VERSION=\"${PROJECT_VERSION}\""
    )
    if(TARGET Qt6::Test)
        list(APPEND _defs QT_TESTLIB_LIB)
    endif()
    if(WIN32)
        list(APPEND _defs UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    foreach(_tgt IN ITEMS Qt6::Platform Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
        if(NOT TARGET ${_tgt})
            continue()
        endif()
        get_target_property(_tgt_defs ${_tgt} INTERFACE_COMPILE_DEFINITIONS)
        if(_tgt_defs)
            foreach(_d IN LISTS _tgt_defs)
                if(NOT _d MATCHES "\\$<")
                    list(APPEND _defs "${_d}")
                endif()
            endforeach()
        endif()
    endforeach()
    list(REMOVE_DUPLICATES _defs)

    # clangd is Clang even when the project is built with MSVC, so the database
    # uses Clang flags. --target=windows-msvc is what lets it find windows.h.
    set(_args clang++)
    list(APPEND _args -std=c++20 -xc++)
    if(WIN32)
        list(APPEND _args --target=x86_64-pc-windows-msvc -fms-extensions -fms-compatibility)
    endif()
    foreach(_inc IN LISTS _includes)
        file(TO_CMAKE_PATH "${_inc}" _inc)
        list(APPEND _args "-I${_inc}")
    endforeach()
    foreach(_d IN LISTS _defs)
        list(APPEND _args "-D${_d}")
    endforeach()

    set(_sources
        ${USBRESTORE_CORE_SOURCES}
        ${USBRESTORE_PLATFORM_SOURCES}
        ${USBRESTORE_GUI_SOURCES}
        tests/test_core.cpp
        tools/partition_dump.cpp
    )
    if(WIN32)
        list(APPEND _sources ${USBRESTORE_WIN_SOURCES})
    else()
        list(APPEND _sources
            ${USBRESTORE_LINUX_SOURCES}
            src/helper/main.cpp
        )
    endif()

    set(_abs_sources "")
    foreach(_src IN LISTS _sources)
        if(NOT _src MATCHES "\\.(cpp|h)$")
            continue()
        endif()
        if(NOT IS_ABSOLUTE "${_src}")
            set(_src "${CMAKE_SOURCE_DIR}/${_src}")
        endif()
        list(APPEND _abs_sources "${_src}")
    endforeach()
    list(REMOVE_DUPLICATES _abs_sources)

    set(_args_json "")
    set(_sep "")
    foreach(_a IN LISTS _args)
        usbrestore_json_escape("${_a}" _esc)
        string(APPEND _args_json "${_sep}\"${_esc}\"")
        set(_sep ", ")
    endforeach()

    file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" _dir)
    usbrestore_json_escape("${_dir}" _dir_esc)

    set(_json "[\n")
    set(_first TRUE)
    foreach(_src IN LISTS _abs_sources)
        file(TO_CMAKE_PATH "${_src}" _src_fwd)
        usbrestore_json_escape("${_src_fwd}" _src_esc)
        if(NOT _first)
            string(APPEND _json ",\n")
        endif()
        set(_first FALSE)
        string(APPEND _json "  {\n")
        string(APPEND _json "    \"directory\": \"${_dir_esc}\",\n")
        string(APPEND _json "    \"file\": \"${_src_esc}\",\n")
        string(APPEND _json "    \"arguments\": [${_args_json}, \"${_src_esc}\"]\n")
        string(APPEND _json "  }")
    endforeach()
    string(APPEND _json "\n]\n")

    set(_out "${CMAKE_SOURCE_DIR}/compile_commands.json")
    file(WRITE "${_out}" "${_json}")
    message(STATUS "Wrote ${_out} for clangd")
endfunction()

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

if(WIN32)
    # The version resource is generated so PROJECT_VERSION stays the only place
    # a release is bumped.
    set(USBRESTORE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
    set(USBRESTORE_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

    add_executable(usb-restoration-tool WIN32
        ${USBRESTORE_CORE_SOURCES}
        ${USBRESTORE_PLATFORM_SOURCES}
        ${USBRESTORE_WIN_SOURCES}
        ${USBRESTORE_GUI_SOURCES}
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    )
else()
    add_executable(usb-restoration-tool
        ${USBRESTORE_CORE_SOURCES}
        ${USBRESTORE_PLATFORM_SOURCES}
        ${USBRESTORE_LINUX_SOURCES}
        ${USBRESTORE_GUI_SOURCES}
    )
endif()

target_include_directories(usb-restoration-tool PRIVATE src)
target_compile_definitions(usb-restoration-tool PRIVATE
    USBRESTORE_APP_VERSION="${PROJECT_VERSION}"
)
if(NOT WIN32)
    target_compile_definitions(usb-restoration-tool PRIVATE
        USBRESTORE_HELPER_PATH="${USBRESTORE_HELPER_PATH}"
    )
endif()
target_link_libraries(usb-restoration-tool PRIVATE Qt6::Core Qt6::Widgets)

if(WIN32)
    target_compile_definitions(usb-restoration-tool PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(usb-restoration-tool PRIVATE ole32 oleaut32 wbemuuid advapi32 shell32 uuid)

    # The manifest is embedded through the .rc file, so the linker must not add
    # one of its own.
    target_link_options(usb-restoration-tool PRIVATE "/MANIFEST:NO")
endif()

usbrestore_harden(usb-restoration-tool)

set_target_properties(usb-restoration-tool PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:usb-restoration-tool>"
)

# The privileged half, on Linux only: one restore per invocation, no GUI.
# Qt6::Core and nothing else, so that none of the widget, theming, image-format
# or input-method machinery the GUI drags in is ever loaded as root. Windows
# gets no equivalent — see the "Not in scope" section of docs/polkit-helper.md.
if(NOT WIN32)
    add_executable(usb-restoration-helper
        ${USBRESTORE_CORE_SOURCES}
        ${USBRESTORE_LINUX_RESTORE_SOURCES}
        src/platform/disk_service.h
        src/platform/startup.h
        src/helper/main.cpp
    )
    target_include_directories(usb-restoration-helper PRIVATE src)
    target_compile_definitions(usb-restoration-helper PRIVATE
        USBRESTORE_APP_VERSION="${PROJECT_VERSION}"
    )
    target_link_libraries(usb-restoration-helper PRIVATE Qt6::Core)
    usbrestore_harden(usb-restoration-helper)
endif()

if(WIN32)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_INSTALL_PREFIX}/bin")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(
            TARGET usb-restoration-tool
            POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}" --no-translations "$<TARGET_FILE:usb-restoration-tool>"
            COMMENT "Deploying Qt runtime files"
        )
    endif()
endif()

if(USBRESTORE_BUILD_TESTS)
    enable_testing()

    add_executable(usb-restoration-tool-tests
        ${USBRESTORE_CORE_SOURCES}
        tests/test_core.cpp
    )
    target_include_directories(usb-restoration-tool-tests PRIVATE src)
    target_link_libraries(usb-restoration-tool-tests PRIVATE Qt6::Core Qt6::Test)
    if(MSVC)
        target_compile_definitions(usb-restoration-tool-tests PRIVATE UNICODE _UNICODE NOMINMAX)
        target_compile_options(usb-restoration-tool-tests PRIVATE /utf-8)
    endif()

    add_test(NAME usb-restoration-tool-tests COMMAND usb-restoration-tool-tests)
    if(WIN32)
        set_tests_properties(usb-restoration-tool-tests PROPERTIES
            ENVIRONMENT "PATH=${QT_INSTALL_PREFIX}/bin;$ENV{PATH}")
    endif()

    # Writes a partition table into a sparse image so sgdisk and fdisk can be
    # asked whether they accept it. The unit tests check the bytes against the
    # specification; scripts/verify-partition-tables.sh checks them against the
    # tools that will have to read a restored disk.
    add_executable(usb-partition-dump
        ${USBRESTORE_CORE_SOURCES}
        tools/partition_dump.cpp
    )
    target_include_directories(usb-partition-dump PRIVATE src)
    target_link_libraries(usb-partition-dump PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_definitions(usb-partition-dump PRIVATE UNICODE _UNICODE NOMINMAX)
        target_compile_options(usb-partition-dump PRIVATE /utf-8)
    endif()
endif()

if(WIN32)
    install(TARGETS usb-restoration-tool RUNTIME DESTINATION ".")
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                --no-translations --no-compiler-runtime
                \"\${CMAKE_INSTALL_PREFIX}/usb-restoration-tool.exe\")
        ")
    endif()
else()
    install(TARGETS usb-restoration-tool RUNTIME DESTINATION bin)

    # libexecdir rather than bin: this is not a command anyone runs, and the
    # polkit action that authorises it names an absolute path.
    #
    # The action goes in with it. Installing the helper without the policy would
    # leave a binary nothing can obtain privilege through, and installing the
    # policy without the helper would leave an action pointing at nothing, so
    # the two are deliberately not separable.
    if(USBRESTORE_INSTALL_HELPER)
        install(TARGETS usb-restoration-helper RUNTIME DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")

        set(USBRESTORE_POLICY_FILE
            "${CMAKE_CURRENT_BINARY_DIR}/dev.karoqdave.usb-restoration-tool.policy")
        configure_file(resources/polkit.policy.in "${USBRESTORE_POLICY_FILE}" @ONLY)
        install(FILES "${USBRESTORE_POLICY_FILE}"
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/polkit-1/actions")
    endif()

    install(FILES resources/usb-restoration-tool.desktop DESTINATION share/applications)
    install(FILES resources/icons/app.svg DESTINATION share/icons/hicolor/scalable/apps
            RENAME usb-restoration-tool.svg)
    install(FILES resources/icons/app.png DESTINATION share/icons/hicolor/256x256/apps
            RENAME usb-restoration-tool.png)
endif()

usbrestore_export_clangd_compile_commands()
