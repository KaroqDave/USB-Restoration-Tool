cmake_minimum_required(VERSION 3.21)

project(USBRestorationTool
    VERSION 1.9.0
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

# Ninja and the Makefile generators honour this; Visual Studio ignores it, so the
# windows preset uses Ninja. clangd finds build/compile_commands.json by itself;
# usbrestore_setup_clangd() links build-linux's next to this file.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

option(USBRESTORE_BUILD_TESTS "Build the USB Restoration Tool C++ tests" ON)

# CI aids for the test targets only; neither belongs in a build anyone runs.
option(USBRESTORE_SANITIZE "Build the tests and tools with AddressSanitizer and UBSan (GCC or Clang)" OFF)
option(USBRESTORE_FUZZ "Build usb-core-fuzz against libFuzzer instead of as a corpus replayer (Clang)" OFF)
if(USBRESTORE_SANITIZE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "USBRESTORE_SANITIZE needs GCC or Clang.")
endif()
if(USBRESTORE_FUZZ AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "USBRESTORE_FUZZ needs Clang, which ships libFuzzer.")
endif()

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

set(_usbrestore_qt_components Core Widgets LinguistTools)
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
    src/core/language.cpp
    src/core/language.h
    src/core/partition_table.cpp
    src/core/partition_table.h
    src/core/restore_message.cpp
    src/core/restore_message.h
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
    src/platform/translations.cpp
    src/platform/translations.h
)

set(USBRESTORE_WIN_SOURCES
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
        # has them.
        target_compile_options(${target} PRIVATE /W4 /permissive- /sdl /guard:cf)
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

# Not applied to the GUI or helper: they keep usbrestore_harden(), and
# _FORTIFY_SOURCE does not combine cleanly with ASan.
function(usbrestore_sanitize target)
    if(USBRESTORE_SANITIZE)
        set(_flags -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
        target_compile_options(${target} PRIVATE ${_flags})
        target_link_options(${target} PRIVATE ${_flags})
    endif()
endfunction()

# What every target compiled from src/ needs. /utf-8 so the check marks and
# separators in the UI strings are compiled as the UTF-8 that QStringLiteral expects.
function(usbrestore_configure target)
    target_include_directories(${target} PRIVATE src)
    target_compile_definitions(${target} PRIVATE USBRESTORE_APP_VERSION="${PROJECT_VERSION}")
    if(WIN32)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    endif()
    if(MSVC)
        target_compile_options(${target} PRIVATE /utf-8)
    endif()
endfunction()

# Writes into the source tree without failing configure on a read-only
# checkout. The payload is produced in the build tree, which is always
# writable; the copy is best-effort.
function(usbrestore_try_copy_to_source abs_src dest_relpath ok_message)
    set(_dest "${CMAKE_CURRENT_SOURCE_DIR}/${dest_relpath}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${abs_src}" "${_dest}"
        RESULT_VARIABLE _res
        ERROR_VARIABLE _err
        OUTPUT_QUIET
    )
    if(_res EQUAL 0)
        message(STATUS "${ok_message}")
    else()
        message(STATUS "Skipping ${_dest}: source tree is not writable")
    endif()
endfunction()

# Clangd still opens files that are not in compile_commands.json, using the
# fallback flags in the root .clangd. On Linux that means src/win is parsed
# without windows.h; on Windows, src/linux and the helper without the Linux
# headers. A directory .clangd (gitignored) suppresses that noise for the
# backend this host does not compile.
function(usbrestore_write_clangd_inactive_platform_skip)
    set(_skip_yaml
[=[# Generated by CMake: this backend is not compiled on this host.
# Clangd still opens these files with fallback flags; suppress the resulting
# missing-system-header diagnostics.
Index:
  Background: Skip
Diagnostics:
  Suppress: ['*']
]=])
    if(WIN32)
        set(_skip_dirs linux helper)
        set(_keep_dirs win)
    else()
        set(_skip_dirs win)
        set(_keep_dirs linux helper)
    endif()
    foreach(_dir IN LISTS _keep_dirs)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E rm -f "${CMAKE_CURRENT_SOURCE_DIR}/src/${_dir}/.clangd"
            ERROR_QUIET
        )
    endforeach()
    set(_yaml_path "${CMAKE_CURRENT_BINARY_DIR}/clangd-inactive-platform.yaml")
    file(WRITE "${_yaml_path}" "${_skip_yaml}")
    foreach(_dir IN LISTS _skip_dirs)
        usbrestore_try_copy_to_source(
            "${_yaml_path}"
            "src/${_dir}/.clangd"
            "Wrote ${CMAKE_CURRENT_SOURCE_DIR}/src/${_dir}/.clangd so clangd skips this backend"
        )
    endforeach()
endfunction()

function(usbrestore_link_compile_commands)
    set(_src "${CMAKE_CURRENT_BINARY_DIR}/compile_commands.json")
    set(_dst "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${_dst}"
        RESULT_VARIABLE _rm_res
        ERROR_QUIET
        OUTPUT_QUIET
    )
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E create_symlink "${_src}" "${_dst}"
        RESULT_VARIABLE _link_res
        ERROR_VARIABLE _link_err
        OUTPUT_QUIET
    )
    if(_link_res EQUAL 0)
        message(STATUS "Linked ${_dst} -> ${_src} for clangd")
    else()
        message(STATUS "Could not link compile_commands.json for clangd: ${_link_err}")
    endif()
endfunction()

function(usbrestore_setup_clangd)
    usbrestore_write_clangd_inactive_platform_skip()
    if(NOT CMAKE_EXPORT_COMPILE_COMMANDS OR CMAKE_GENERATOR MATCHES "Visual Studio")
        return()
    endif()
    if(WIN32)
        # A database synthesised by an older configure would shadow build/'s.
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json"
                AND NOT IS_SYMLINK "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
            file(REMOVE "${CMAKE_CURRENT_SOURCE_DIR}/compile_commands.json")
        endif()
    else()
        usbrestore_link_compile_commands()
    endif()
endfunction()

set(USBRESTORE_GUI_SOURCES
    src/gui/app_settings.cpp
    src/gui/app_settings.h
    src/gui/main.cpp
    src/gui/main_window.cpp
    src/gui/main_window.h
    src/gui/restore_dialog.cpp
    src/gui/restore_dialog.h
    src/gui/theme.cpp
    src/gui/theme.h
    resources/resources.qrc
)

# src/core once, hardened like the binaries that hold privilege, and once more
# under the sanitizers when the tests use them: ASan and _FORTIFY_SOURCE do not
# combine. The fuzzer compiles its own copy so libFuzzer can instrument it.
function(usbrestore_add_core target)
    add_library(${target} STATIC ${USBRESTORE_CORE_SOURCES})
    usbrestore_configure(${target})
    target_link_libraries(${target} PUBLIC Qt6::Core)
endfunction()

usbrestore_add_core(usbrestore-core)
usbrestore_harden(usbrestore-core)
set(USBRESTORE_TEST_CORE usbrestore-core)
if(USBRESTORE_BUILD_TESTS AND USBRESTORE_SANITIZE)
    usbrestore_add_core(usbrestore-core-sanitized)
    usbrestore_sanitize(usbrestore-core-sanitized)
    set(USBRESTORE_TEST_CORE usbrestore-core-sanitized)
endif()

add_executable(usb-branding-export EXCLUDE_FROM_ALL tools/export_branding.cpp)
target_link_libraries(usb-branding-export PRIVATE Qt6::Gui)

if(NOT WIN32)
    add_executable(usb-qt-bundle-probe EXCLUDE_FROM_ALL tools/qt_bundle_probe.cpp)
    # No build rpath: the AppImage check selects bundled Qt with LD_LIBRARY_PATH,
    # and a DT_RPATH would be searched first and hide a damaged bundle.
    set_target_properties(usb-qt-bundle-probe PROPERTIES SKIP_BUILD_RPATH TRUE)
    target_link_libraries(usb-qt-bundle-probe PRIVATE Qt6::Widgets)
endif()

if(WIN32)
    # The version resource is generated so PROJECT_VERSION stays the only place
    # a release is bumped.
    set(USBRESTORE_APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app.ico")
    set(USBRESTORE_APP_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.manifest")
    configure_file(resources/app.rc.in "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)

    add_executable(usb-restoration-tool WIN32
        ${USBRESTORE_PLATFORM_SOURCES}
        ${USBRESTORE_WIN_SOURCES}
        ${USBRESTORE_GUI_SOURCES}
        "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    )
else()
    add_executable(usb-restoration-tool
        ${USBRESTORE_PLATFORM_SOURCES}
        ${USBRESTORE_LINUX_SOURCES}
        ${USBRESTORE_GUI_SOURCES}
    )
endif()

usbrestore_configure(usb-restoration-tool)
if(NOT WIN32)
    target_compile_definitions(usb-restoration-tool PRIVATE
        USBRESTORE_HELPER_PATH="${USBRESTORE_HELPER_PATH}"
    )
endif()
target_link_libraries(usb-restoration-tool PRIVATE usbrestore-core Qt6::Widgets)

if(Qt6_VERSION VERSION_GREATER_EQUAL "6.7")
    set(_usbrestore_translation_source_targets SOURCE_TARGETS usb-restoration-tool)
endif()
# Both backends, whichever this build compiles, so one catalogue serves both.
set(_usbrestore_translation_sources
    ${USBRESTORE_CORE_SOURCES} ${USBRESTORE_PLATFORM_SOURCES} ${USBRESTORE_GUI_SOURCES}
    ${USBRESTORE_LINUX_SOURCES} ${USBRESTORE_WIN_SOURCES} src/helper/main.cpp)
list(FILTER _usbrestore_translation_sources INCLUDE REGEX "\\.(cpp|h)$")
qt_add_translations(usb-restoration-tool
    ${_usbrestore_translation_source_targets}
    TS_FILES translations/usbrestore_de.ts
    SOURCES ${_usbrestore_translation_sources}
    INCLUDE_DIRECTORIES src
    LUPDATE_OPTIONS -locations none -no-obsolete
    LRELEASE_OPTIONS -nounfinished
    QM_FILES_OUTPUT_VARIABLE USBRESTORE_QM_FILES)
unset(_usbrestore_translation_source_targets)
unset(_usbrestore_translation_sources)

# Qt before 6.7 names this target after the executable rather than the project.
set(USBRESTORE_LRELEASE_TARGET ${PROJECT_NAME}_lrelease)
if(NOT TARGET ${USBRESTORE_LRELEASE_TARGET})
    set(USBRESTORE_LRELEASE_TARGET usb-restoration-tool_lrelease)
endif()

find_file(USBRESTORE_QTBASE_DE_QM qtbase_de.qm
    HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}"
          "${QT_INSTALL_PREFIX}/translations" "${QT_INSTALL_PREFIX}/share/qt6/translations"
    NO_DEFAULT_PATH REQUIRED)

function(usbrestore_embed_translations target)
    # Without this, parallel Makefile builds run lrelease once per target and rcc
    # can read a catalog that another target is still rewriting.
    add_dependencies(${target} ${USBRESTORE_LRELEASE_TARGET})
    qt_add_resources(${target} "${target}_translations"
        PREFIX "/i18n"
        BASE "${CMAKE_CURRENT_BINARY_DIR}"
        FILES ${USBRESTORE_QM_FILES})
    get_filename_component(_qt_translations_dir "${USBRESTORE_QTBASE_DE_QM}" DIRECTORY)
    qt_add_resources(${target} "${target}_qt_translations"
        PREFIX "/i18n"
        BASE "${_qt_translations_dir}"
        FILES "${USBRESTORE_QTBASE_DE_QM}")
endfunction()

usbrestore_embed_translations(usb-restoration-tool)

if(WIN32)
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
        ${USBRESTORE_LINUX_RESTORE_SOURCES}
        src/platform/disk_service.h
        src/platform/startup.h
        src/platform/translations.cpp
        src/helper/main.cpp
    )
    usbrestore_configure(usb-restoration-helper)
    target_link_libraries(usb-restoration-helper PRIVATE usbrestore-core)
    usbrestore_embed_translations(usb-restoration-helper)
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

    add_executable(usb-restoration-tool-tests src/platform/translations.cpp tests/test_core.cpp)
    usbrestore_embed_translations(usb-restoration-tool-tests)
    usbrestore_configure(usb-restoration-tool-tests)
    target_link_libraries(usb-restoration-tool-tests PRIVATE ${USBRESTORE_TEST_CORE} Qt6::Test)
    usbrestore_sanitize(usb-restoration-tool-tests)

    if(NOT WIN32)
        add_executable(usb-linux-tests tests/test_linux.cpp src/platform/translations.cpp
            src/linux/linux_disk_service.cpp src/linux/helper_client.cpp ${USBRESTORE_LINUX_RESTORE_SOURCES})
        usbrestore_configure(usb-linux-tests)
        target_compile_definitions(usb-linux-tests PRIVATE USBRESTORE_HELPER_PATH="${USBRESTORE_HELPER_PATH}")
        target_link_libraries(usb-linux-tests PRIVATE ${USBRESTORE_TEST_CORE} Qt6::Test)
        usbrestore_embed_translations(usb-linux-tests)
        usbrestore_sanitize(usb-linux-tests)
        add_dependencies(usb-restoration-tool-tests usb-linux-tests)
        add_test(NAME usb-linux-tests COMMAND usb-linux-tests)
    endif()

    add_test(NAME usb-restoration-tool-tests COMMAND usb-restoration-tool-tests)

    add_executable(usb-gui-tests
        src/platform/logger.cpp
        src/platform/logger.h
        src/platform/restore_worker.cpp
        src/platform/restore_worker.h
        src/platform/translations.cpp
        src/platform/translations.h
        src/gui/app_settings.cpp
        src/gui/main_window.cpp
        src/gui/main_window.h
        src/gui/restore_dialog.cpp
        src/gui/restore_dialog.h
        src/gui/theme.cpp
        resources/resources.qrc
        tests/test_gui.cpp
    )
    usbrestore_configure(usb-gui-tests)
    target_link_libraries(usb-gui-tests PRIVATE ${USBRESTORE_TEST_CORE} Qt6::Widgets Qt6::Test)
    usbrestore_embed_translations(usb-gui-tests)
    if(WIN32)
        target_sources(usb-gui-tests PRIVATE src/win/windows_util.cpp)
        target_link_libraries(usb-gui-tests PRIVATE shell32 advapi32)
    else()
        add_dependencies(usb-gui-tests usb-qt-bundle-probe)
    endif()
    usbrestore_sanitize(usb-gui-tests)
    add_dependencies(usb-restoration-tool-tests usb-gui-tests)
    add_test(NAME usb-gui-tests COMMAND usb-gui-tests -platform offscreen)
    add_test(NAME usb-gui-tests-scaled COMMAND usb-gui-tests rendersBothPlansAndThemes rendersTheMainWindow translatesMainWindowDialogsAndErrors
                showsTranslatedSafetyRefusalsWithoutStartingARestore -platform offscreen)
    set_tests_properties(usb-gui-tests-scaled PROPERTIES ENVIRONMENT "QT_SCALE_FACTOR=2")
    if(WIN32)
        # ENVIRONMENT is a CMake list, so a raw Windows PATH would be split on ';'.
        set(_usbrestore_test_path "${QT_INSTALL_PREFIX}/bin;$ENV{PATH}")
        string(REPLACE ";" "\\;" _usbrestore_test_path "${_usbrestore_test_path}")
        set_target_properties(usb-gui-tests PROPERTIES WIN32_EXECUTABLE OFF)
        set_tests_properties(usb-gui-tests PROPERTIES
            ENVIRONMENT "PATH=${_usbrestore_test_path};QT_FORCE_STDERR_LOGGING=1")
        set_property(TEST usb-gui-tests-scaled APPEND PROPERTY
            ENVIRONMENT "PATH=${_usbrestore_test_path};QT_FORCE_STDERR_LOGGING=1")
        set_tests_properties(usb-restoration-tool-tests PROPERTIES
            ENVIRONMENT "PATH=${_usbrestore_test_path};QT_FORCE_STDERR_LOGGING=1")

        add_executable(usb-wmi-tests tests/test_wmi.cpp src/platform/translations.cpp ${USBRESTORE_WIN_SOURCES})
        usbrestore_configure(usb-wmi-tests)
        usbrestore_embed_translations(usb-wmi-tests)
        target_link_libraries(usb-wmi-tests PRIVATE
            ${USBRESTORE_TEST_CORE} Qt6::Test ole32 oleaut32 wbemuuid advapi32 shell32 uuid)
        add_dependencies(usb-restoration-tool-tests usb-wmi-tests)
        add_test(NAME usb-wmi-tests COMMAND usb-wmi-tests)
        set_tests_properties(usb-wmi-tests PROPERTIES
            ENVIRONMENT "PATH=${_usbrestore_test_path};QT_FORCE_STDERR_LOGGING=1")
    endif()

    # Writes a partition table into a sparse image so sgdisk and fdisk can be
    # asked whether they accept it. The unit tests check the bytes against the
    # specification; scripts/verify-partition-tables.sh checks them against the
    # tools that will have to read a restored disk.
    add_executable(usb-partition-dump tools/partition_dump.cpp)
    usbrestore_configure(usb-partition-dump)
    target_link_libraries(usb-partition-dump PRIVATE ${USBRESTORE_TEST_CORE})
    usbrestore_sanitize(usb-partition-dump)

    # Replays tests/fuzz-corpus as an ordinary test; USBRESTORE_FUZZ turns the
    # same source into a libFuzzer target that also accepts those files.
    add_executable(usb-core-fuzz
        ${USBRESTORE_CORE_SOURCES}
        tests/fuzz_core.cpp
    )
    usbrestore_configure(usb-core-fuzz)
    target_link_libraries(usb-core-fuzz PRIVATE Qt6::Core)
    if(USBRESTORE_FUZZ)
        target_compile_definitions(usb-core-fuzz PRIVATE USBRESTORE_LIBFUZZER)
        target_compile_options(usb-core-fuzz PRIVATE -fsanitize=fuzzer)
        target_link_options(usb-core-fuzz PRIVATE -fsanitize=fuzzer)
    endif()
    usbrestore_sanitize(usb-core-fuzz)
    # CI and release builds name their targets; building the tests must bring
    # the replayer along, or ctest reports its test as "Not Run".
    add_dependencies(usb-restoration-tool-tests usb-core-fuzz)

    file(GLOB _usbrestore_fuzz_seeds CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz-corpus/*")
    add_test(NAME usb-core-fuzz-corpus COMMAND usb-core-fuzz ${_usbrestore_fuzz_seeds})
    if(WIN32)
        set_tests_properties(usb-core-fuzz-corpus PROPERTIES
            ENVIRONMENT "PATH=${_usbrestore_test_path}")
    endif()
    unset(_usbrestore_fuzz_seeds)
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

    # A package without the helper would be the AppImage's run-as-root mode
    # with none of the AppImage's portability, so there is none.
    if(USBRESTORE_INSTALL_HELPER)
        install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/licenses/usb-restoration-tool")
        # Debian looks for the license at this path. The RPM keeps the copy above,
        # marked %license; one install tree feeds both packages.
        install(FILES LICENSE
                DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/usb-restoration-tool"
                RENAME copyright)

        set(CPACK_GENERATOR DEB)
        set(CPACK_PACKAGE_NAME usb-restoration-tool)
        set(CPACK_PACKAGE_VENDOR KaroqDave)
        set(CPACK_PACKAGE_CONTACT "KaroqDave <47065562+KaroqDave@users.noreply.github.com>")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
        string(CONCAT CPACK_PACKAGE_DESCRIPTION
            "Erases an ISO-written USB drive and leaves one GPT or MBR partition formatted exFAT, FAT32, NTFS or "
            "ext4. The interface runs unprivileged; the restore itself runs in a separate helper started "
            "through pkexec.")
        set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/KaroqDave/USB-Restoration-Tool")
        set(CPACK_PACKAGING_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
        set(CPACK_USBRESTORE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")
        set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cpack_project_config.cmake")
        set(CPACK_STRIP_FILES ON)
        set(CPACK_PACKAGE_RELOCATABLE OFF)

        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_SECTION admin)
        # dpkg-shlibdeps fails on a Qt that no package owns, which keeps a
        # build against a private Qt kit from producing a .deb.
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        # Not linked, so shlibdeps cannot see them: Qt's platform and SVG
        # plugins, pkexec, and the formatters linux_restore.cpp looks for.
        string(JOIN ", " CPACK_DEBIAN_PACKAGE_DEPENDS
            qt6-qpa-plugins
            "qt6-svg-plugins | libqt6svg6"
            "pkexec | policykit-1"
            "exfatprogs | exfat-utils"
            dosfstools
            ntfs-3g
            e2fsprogs)

        set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
        set(CPACK_RPM_PACKAGE_RELEASE 1)
        set(CPACK_RPM_PACKAGE_RELEASE_DIST ON)
        set(CPACK_RPM_PACKAGE_LICENSE MIT)
        set(CPACK_RPM_PACKAGE_GROUP "System/Tools")
        set(CPACK_RPM_PACKAGE_AUTOREQ ON)
        # RPM discovers linked libraries, not Qt plugins or external programs.
        string(JOIN ", " CPACK_RPM_PACKAGE_REQUIRES
            qt6-qtbase-gui
            qt6-qtwayland
            qt6-qtsvg
            polkit
            exfatprogs
            dosfstools
            ntfsprogs
            e2fsprogs)
        set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
            /usr/libexec
            /usr/share/applications
            /usr/share/icons
            /usr/share/icons/hicolor
            /usr/share/icons/hicolor/scalable
            /usr/share/icons/hicolor/scalable/apps
            /usr/share/icons/hicolor/256x256
            /usr/share/icons/hicolor/256x256/apps
            /usr/share/licenses
            /usr/share/polkit-1
            /usr/share/polkit-1/actions)
        set(CPACK_RPM_USER_FILELIST "%license /usr/share/licenses/usb-restoration-tool/LICENSE")

        include(CPack)
    endif()
endif()

usbrestore_setup_clangd()
