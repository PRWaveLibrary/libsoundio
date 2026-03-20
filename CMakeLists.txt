cmake_minimum_required(VERSION 4.0)
project(libsoundio LANGUAGES CXX)
if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

set(CMAKE_MODULE_PATH ${libsoundio_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

include(GNUInstallDirs)

if (NOT CMAKE_SOUNDIO_BUILD_TYPE)
    set(CMAKE_SOUNDIO_BUILD_TYPE "Debug" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif ()

set(LIBSOUNDIO_VERSION_MAJOR 1)
set(LIBSOUNDIO_VERSION_MINOR 1)
set(LIBSOUNDIO_VERSION_PATCH 0)
set(LIBSOUNDIO_VERSION "${LIBSOUNDIO_VERSION_MAJOR}.${LIBSOUNDIO_VERSION_MINOR}.${LIBSOUNDIO_VERSION_PATCH}")
message("Configuring libsoundio version ${LIBSOUNDIO_VERSION}")

if (NOT SOUNDIO_STATIC_LIBNAME)
    set(SOUNDIO_STATIC_LIBNAME soundio)
endif ()

option(SOUNDIO_BUILD_STATIC_LIBS "Build static libraries" ON)
option(SOUNDIO_BUILD_DYNAMIC_LIBS "Build dynamic libraries" ON)
option(SOUNDIO_BUILD_EXAMPLE_PROGRAMS "Build example programs" ON)
option(SOUNDIO_BUILD_TESTS "Build tests" ON)
option(SOUNDIO_ENABLE_COREAUDIO "Enable CoreAudio backend" ON)
option(SOUNDIO_ENABLE_COREAUDIO_IOS "Enable CoreAudio iOS backend" ON)
option(SOUNDIO_ENABLE_OBOE "Enable Android Oboe backend" ON)
option(SOUNDIO_ENABLE_WASAPI "Enable WASAPI backend" ON)

find_package(Threads)
if (Threads_FOUND)
    set(STATUS_THREADS "OK")
else (Threads_FOUND)
    set(STATUS_THREADS "not found")
endif (Threads_FOUND)

if (SOUNDIO_ENABLE_COREAUDIO AND NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    find_package(COREAUDIO)
    if (COREAUDIO_FOUND)
        set(STATUS_COREAUDIO "OK")
        set(SOUNDIO_HAVE_COREAUDIO true)
        include_directories(${COREAUDIO_INCLUDE_DIR})

        find_path(COREFOUNDATION_INCLUDE_DIR NAMES CoreFoundation.h)
        find_library(COREFOUNDATION_LIBRARY NAMES CoreFoundation)
        include_directories(${COREFOUNDATION_INCLUDE_DIR})

        find_path(AUDIOUNIT_INCLUDE_DIR NAMES AudioUnit.h)
        find_library(AUDIOUNIT_LIBRARY NAMES AudioUnit)
        include_directories(${AUDIOUNIT_INCLUDE_DIR})
    else ()
        set(STATUS_COREAUDIO "not found")
        set(SOUNDIO_HAVE_COREAUDIO false)
        set(COREAUDIO_LIBRARY "")
        set(COREFOUNDATION_LIBRARY "")
        set(AUDIOUNIT_LIBRARY "")
    endif ()
else ()
    set(STATUS_COREAUDIO "disabled")
    set(SOUNDIO_HAVE_COREAUDIO false)
    set(COREAUDIO_LIBRARY "")
    set(COREFOUNDATION_LIBRARY "")
    set(AUDIOUNIT_LIBRARY "")
endif ()

if (SOUNDIO_ENABLE_COREAUDIO_IOS AND CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(STATUS_COREAUDIO_IOS "OK")
    set(SOUNDIO_HAVE_COREAUDIO_IOS true)
else ()
    set(STATUS_COREAUDIO_IOS "disabled")
    set(SOUNDIO_HAVE_COREAUDIO_IOS false)
endif ()

if (SOUNDIO_ENABLE_OBOE AND CMAKE_SYSTEM_NAME STREQUAL "Android")
    # Build Oboe from source
    add_subdirectory(${libsoundio_SOURCE_DIR}/third_party/oboe ${libsoundio_BINARY_DIR}/oboe)
    # Oboe include path for libsoundio sources
    include_directories(${libsoundio_SOURCE_DIR}/third_party/oboe/include)
    set(STATUS_OBOE "OK")
    set(SOUNDIO_HAVE_OBOE true)
    set(OBOE_LIBRARY oboe)
else ()
    set(STATUS_OBOE "disabled")
    set(SOUNDIO_HAVE_OBOE false)
    set(OBOE_LIBRARY "")
endif ()

if (SOUNDIO_ENABLE_WASAPI)
    find_package(WASAPI)
    if (WASAPI_FOUND)
        set(STATUS_WASAPI "OK")
        set(SOUNDIO_HAVE_WASAPI true)
    else ()
        set(STATUS_WASAPI "not found")
        set(SOUNDIO_HAVE_WASAPI false)
    endif ()
else ()
    set(STATUS_WASAPI "disabled")
    set(SOUNDIO_HAVE_WASAPI false)
endif ()


set(LIBSOUNDIO_SOURCES
        "${libsoundio_SOURCE_DIR}/src/soundio.cpp"
        "${libsoundio_SOURCE_DIR}/src/util.cpp"
        "${libsoundio_SOURCE_DIR}/src/os.cpp"
        "${libsoundio_SOURCE_DIR}/src/dummy.cpp"
        "${libsoundio_SOURCE_DIR}/src/channel_layout.cpp"
        "${libsoundio_SOURCE_DIR}/src/ring_buffer.cpp"
)

set(LIBSOUNDIO_EXTEN_INCLUDE_LIB "")

set(CONFIGURE_OUT_FILE "${libsoundio_BINARY_DIR}/config.h")
set(LIBSOUNDIO_HEADERS
        "${libsoundio_SOURCE_DIR}/soundio/soundio.h"
        "${libsoundio_SOURCE_DIR}/soundio/endian.h"
        "${libsoundio_SOURCE_DIR}/logger/logger.hpp"
)

if (SOUNDIO_HAVE_COREAUDIO)
    list(APPEND LIBSOUNDIO_SOURCES "${libsoundio_SOURCE_DIR}/src/coreaudio.cpp")
endif ()

if (SOUNDIO_HAVE_COREAUDIO_IOS)
    list(APPEND LIBSOUNDIO_HEADERS
            "${libsoundio_SOURCE_DIR}/src/soundio_internal.h"
            "${libsoundio_SOURCE_DIR}/src/soundio_private.h"
            "${libsoundio_SOURCE_DIR}/src/coreaudio_ios.h"
            "${libsoundio_SOURCE_DIR}/src/oc_method_swizzling.h"
    )
    list(APPEND LIBSOUNDIO_SOURCES
            ${LIBSOUNDIO_HEADERS}
            ${CONFIGURE_OUT_FILE}
            "${libsoundio_SOURCE_DIR}/src/coreaudio_ios.mm"
            "${libsoundio_SOURCE_DIR}/src/oc_method_swizzling.mm"
    )

    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox REQUIRED)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation REQUIRED)
    find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
    list(APPEND LIBSOUNDIO_EXTEN_INCLUDE_LIB
            ${AUDIOTOOLBOX_FRAMEWORK}
            ${AVFOUNDATION_FRAMEWORK}
            ${FOUNDATION_FRAMEWORK}
    )
endif ()

if (SOUNDIO_HAVE_OBOE)
    list(APPEND LIBSOUNDIO_HEADERS
            "${libsoundio_SOURCE_DIR}/src/soundio_internal.h"
            "${libsoundio_SOURCE_DIR}/src/soundio_private.h"
            "${libsoundio_SOURCE_DIR}/src/oboe_callback.h"
            "${libsoundio_SOURCE_DIR}/src/oboe.h"
    )
    list(APPEND LIBSOUNDIO_SOURCES
            ${CONFIGURE_OUT_FILE}
            "${libsoundio_SOURCE_DIR}/src/oboe_callback.cpp"
            "${libsoundio_SOURCE_DIR}/src/oboe.cpp"
    )
    list(APPEND LIBSOUNDIO_EXTEN_INCLUDE_LIB ${OBOE_LIBRARY})
endif ()

if (SOUNDIO_HAVE_WASAPI)
    list(APPEND LIBSOUNDIO_SOURCES "${libsoundio_SOURCE_DIR}/src/wasapi.cpp")
endif ()

include_directories(
        ${libsoundio_SOURCE_DIR}
        ${libsoundio_BINARY_DIR}
        "${libsoundio_SOURCE_DIR}/test"
        "${libsoundio_SOURCE_DIR}/src"
)

set(LIBSOUNDIO_LIBS
        ${COREAUDIO_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${CMAKE_THREAD_LIBS_INIT}
        ${OBOE_LIBRARY}
)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /Wall")
    set(LIB_CFLAGS "/TP /W1 /GR /EHsc /std:c++20 /Zc:preprocessor /utf-8")
    set(EXAMPLE_CFLAGS "/W4 /std:c++20")
    set(TEST_CFLAGS "${LIB_CFLAGS}")
    set(TEST_LDFLAGS " ")
    set(LIBM " ")
else ()
    set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Werror -pedantic")
    set(LIB_CFLAGS "-fvisibility=hidden -Wall -Werror=strict-prototypes -Werror=old-style-definition -Werror=missing-prototypes -D_REENTRANT -D_POSIX_C_SOURCE=200809L -Wno-missing-braces")
    set(EXAMPLE_CFLAGS "-std=c99 -Wall")
    set(TEST_CFLAGS "${LIB_CFLAGS} -fprofile-arcs -ftest-coverage")
    set(TEST_LDFLAGS "-fprofile-arcs -ftest-coverage")
    set(LIBM "m")
endif ()

configure_file(
        "${libsoundio_SOURCE_DIR}/src/config.h.in"
        ${CONFIGURE_OUT_FILE}
)
set(DOXYGEN_CONF_FILE "${libsoundio_BINARY_DIR}/doxygen.conf")
configure_file("${libsoundio_SOURCE_DIR}/doc/doxygen.conf.in" ${DOXYGEN_CONF_FILE})

if (SOUNDIO_BUILD_DYNAMIC_LIBS)
    add_library(libsoundio_shared SHARED ${LIBSOUNDIO_SOURCES})
    set_target_properties(libsoundio_shared PROPERTIES
            OUTPUT_NAME soundio
            SOVERSION ${LIBSOUNDIO_VERSION_MAJOR}
            VERSION ${LIBSOUNDIO_VERSION}
            COMPILE_FLAGS ${LIB_CFLAGS}
            LINKER_LANGUAGE CXX
    )
    target_link_libraries(libsoundio_shared PUBLIC ${LIBSOUNDIO_LIBS})
    target_link_libraries(libsoundio_shared PRIVATE ${LIBSOUNDIO_EXTEN_INCLUDE_LIB})
    install(TARGETS libsoundio_shared DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif ()

if (SOUNDIO_BUILD_STATIC_LIBS)
    add_library(libsoundio_static STATIC ${LIBSOUNDIO_SOURCES})
    set_target_properties(libsoundio_static PROPERTIES
            OUTPUT_NAME ${SOUNDIO_STATIC_LIBNAME}
            COMPILE_FLAGS ${LIB_CFLAGS}
            LINKER_LANGUAGE CXX
    )
    target_link_libraries(libsoundio_static PRIVATE ${LIBSOUNDIO_EXTEN_INCLUDE_LIB})
    if (SOUNDIO_HAVE_OBOE)
        target_link_libraries(libsoundio_static PUBLIC ${OBOE_LIBRARY})
    endif ()
    install(TARGETS libsoundio_static DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif ()

install(FILES
        ${LIBSOUNDIO_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/soundio")

# Example Programs

if (SOUNDIO_BUILD_EXAMPLE_PROGRAMS)
    foreach (example_name sio_sine sio_list_devices sio_microphone sio_record)
        add_executable(${example_name} "example/${example_name}.c")
        set_target_properties(${example_name} PROPERTIES
                LINKER_LANGUAGE C
                COMPILE_FLAGS ${EXAMPLE_CFLAGS})
        if (SOUNDIO_BUILD_DYNAMIC_LIBS)
            target_link_libraries(${example_name} libsoundio_shared ${LIBM})
        else ()
            target_link_libraries(${example_name} libsoundio_static ${LIBSOUNDIO_LIBS} ${LIBM})
        endif ()
        install(TARGETS ${example_name} DESTINATION ${CMAKE_INSTALL_BINDIR})
    endforeach ()
endif ()

if (SOUNDIO_BUILD_TESTS)
    add_executable(unit_tests "${libsoundio_SOURCE_DIR}/test/unit_tests.c" ${LIBSOUNDIO_SOURCES})
    target_link_libraries(unit_tests LINK_PUBLIC ${LIBSOUNDIO_LIBS})
    set_target_properties(unit_tests PROPERTIES
            LINKER_LANGUAGE C
            COMPILE_FLAGS ${TEST_CFLAGS}
            LINK_FLAGS ${TEST_LDFLAGS}
    )

    add_executable(latency "${libsoundio_SOURCE_DIR}/test/latency.c" ${LIBSOUNDIO_SOURCES})
    target_link_libraries(latency LINK_PUBLIC ${LIBSOUNDIO_LIBS} ${LIBM})
    set_target_properties(latency PROPERTIES
            LINKER_LANGUAGE C
            COMPILE_FLAGS ${LIB_CFLAGS}
    )

    foreach (test_name underflow backend_disconnect_recover overflow)
        add_executable(${test_name} "test/${test_name}.c")
        set_target_properties(${test_name} PROPERTIES
                LINKER_LANGUAGE C
                COMPILE_FLAGS ${EXAMPLE_CFLAGS})
        if (SOUNDIO_BUILD_DYNAMIC_LIBS)
            target_link_libraries(${test_name} libsoundio_shared ${LIBM})
        else ()
            target_link_libraries(${test_name} libsoundio_static ${LIBSOUNDIO_LIBS} ${LIBM})
        endif ()
    endforeach ()


    add_custom_target(coverage
            DEPENDS unit_tests
            WORKING_DIRECTORY ${libsoundio_BINARY_DIR}
            COMMAND lcov --directory . --zerocounters --rc lcov_branch_coverage=1
            COMMAND ./unit_tests
            COMMAND lcov --directory . --capture --output-file coverage.info --rc lcov_branch_coverage=1
            COMMAND lcov --remove coverage.info '/usr/*' --output-file coverage.info.cleaned --rc lcov_branch_coverage=1
            COMMAND genhtml -o coverage coverage.info.cleaned --rc lcov_branch_coverage=1
            COMMAND rm coverage.info coverage.info.cleaned
    )
endif ()


add_custom_target(doc
        WORKING_DIRECTORY ${libsoundio_BINARY_DIR}
        COMMAND doxygen ${DOXYGEN_CONF_FILE}
)


message("\n"
        "Installation Summary\n"
        "--------------------\n"
        "* Install Directory            : ${CMAKE_INSTALL_PREFIX}\n"
        "* Build Type                   : ${CMAKE_SOUNDIO_BUILD_TYPE}\n"
        "* Build static libs            : ${SOUNDIO_BUILD_STATIC_LIBS}\n"
        "* Build examples               : ${SOUNDIO_BUILD_EXAMPLE_PROGRAMS}\n"
        "* Build tests                  : ${SOUNDIO_BUILD_TESTS}\n"
)

message(
        "System Dependencies\n"
        "-------------------\n"
        "* threads                      : ${STATUS_THREADS}\n"
        "* CoreAudio      (optional)    : ${STATUS_COREAUDIO}\n"
        "* CoreAudio iOS  (optional)    : ${STATUS_COREAUDIO_IOS}\n"
        "* Oboe           (optional)    : ${STATUS_OBOE}\n"
        "* WASAPI         (optional)    : ${STATUS_WASAPI}\n"
)