
# User-space libc for the kernel.
#
# Standalone:
#   cmake -S . -B build -G Ninja
#   cmake --build build
#
# From your own project (any directory layout):
#   cmake_minimum_required(VERSION 3.16)
#   set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/libc/cmake/toolchain-i686.cmake)
#   project(myapp C ASM)
#   add_subdirectory(libc)
#   libc_add_program(myapp main.c other.c)      # -> myapp (ELF) and myapp.bin
#
# Options:
#   LIBC_IMAGE_BASE      load address of programs (default 0x40000000)
#   LIBC_BUILD_EXAMPLES  build examples/hello, gfx_demo, clearance, input_demo, sprite_demo  (default ON standalone)
#   LIBC_BUILD_TESTS     host tests via ctest, Linux only (default OFF)

cmake_minimum_required(VERSION 3.16)

# Standalone builds (this is the top-level CMakeLists.txt, e.g. opened in an
# IDE) set up the i386 toolchain right here, so no separate toolchain file is
# needed. A parent project should instead set CMAKE_TOOLCHAIN_FILE to
# cmake/toolchain-i686.cmake before its project() call.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_SYSTEM_NAME Generic)
    set(CMAKE_SYSTEM_PROCESSOR i686)
    if(NOT CMAKE_C_COMPILER)
        find_program(_LIBC_CROSS_GCC i686-elf-gcc)
        if(_LIBC_CROSS_GCC)
            set(CMAKE_C_COMPILER ${_LIBC_CROSS_GCC})
        else()
            set(CMAKE_C_COMPILER gcc)
        endif()
    endif()
    if(NOT CMAKE_ASM_COMPILER)
        set(CMAKE_ASM_COMPILER ${CMAKE_C_COMPILER})
    endif()
    set(CMAKE_C_FLAGS_INIT   "-m32 -march=i686")
    set(CMAKE_ASM_FLAGS_INIT "-m32 -march=i686")
    set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
endif()

# Everything below lives next to this file; say so plainly if it doesn't
foreach(_libc_required include src user.ld)
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${_libc_required})
        message(FATAL_ERROR
            "libc: ${CMAKE_CURRENT_SOURCE_DIR}/${_libc_required} is missing.\n"
            "Unzip the whole libc folder (include/, src/, user.ld, cmake/, ...) "
            "and point CMake at the folder that holds this CMakeLists.txt.")
    endif()
endforeach()

project(kernel_libc C ASM)

set(_libc_standalone OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_libc_standalone ON)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    endif()
endif()

set(LIBC_IMAGE_BASE "0x40000000" CACHE STRING "Address user programs are linked at")
option(LIBC_BUILD_EXAMPLES "Build the example programs" ${_libc_standalone})
option(LIBC_BUILD_TESTS "Build the host tests (Linux with -m32 support)" OFF)

if(NOT CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "libc needs GCC or Clang (found ${CMAKE_C_COMPILER_ID})")
endif()

#
# Flags
#

# The compiler's own freestanding headers (stddef.h, stdint.h, stdarg.h,
# stdbool.h, float.h); every other header comes from include/
execute_process(
    COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
    OUTPUT_VARIABLE LIBC_COMPILER_INCLUDE
    OUTPUT_STRIP_TRAILING_WHITESPACE)

# Cached so libc_add_program() sees it when called from a parent project
set(LIBC_LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/user.ld CACHE INTERNAL "")

# Shared by the library and the programs that use it
set(_libc_common_options
    -std=gnu11
    -ffreestanding
    -nostdinc
    "SHELL:-isystem ${LIBC_COMPILER_INCLUDE}"
    -fno-pic
    -fno-pie
    -fno-stack-protector
    -fcf-protection=none
    -fno-asynchronous-unwind-tables)

# The library itself: no builtins, and (GCC) no turning the byte loops in
# string.c into calls to memcpy/memset, which would recurse
set(_libc_build_options -fno-builtin -Wall -Wextra)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND _libc_build_options -fno-tree-loop-distribute-patterns)
endif()

set(_libc_link_options
    -nostdlib
    -static
    "-T${LIBC_LINKER_SCRIPT}"
    "-Wl,--defsym=__image_base=${LIBC_IMAGE_BASE}"
    -Wl,--build-id=none
    -Wl,-z,noexecstack
    -Wl,-z,max-page-size=4096)
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    list(APPEND _libc_link_options -no-pie)
endif()

set(_libc_sources
    src/errno.c
    src/unistd.c
    src/string.c
    src/ctype.c
    src/divdi3.c
    src/malloc.c
    src/stdlib.c
    src/start.c
    src/printf.c
    src/stdio.c
    src/dirent.c
    src/gfx.c
    src/security.c
    src/ipc.c
    src/resource.c)

#
# Targets
#
#   libc_crt0   program entry point (object library)
#   c           libc.a
#   libc::libc  what programs link: libc.a + crt0 + headers + flags + user.ld
#

add_library(libc_crt0 OBJECT src/crt0.S)

add_library(c STATIC ${_libc_sources})
target_include_directories(c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(c PRIVATE ${_libc_common_options} ${_libc_build_options})
set_target_properties(c PROPERTIES POSITION_INDEPENDENT_CODE OFF)

add_library(libc_runtime INTERFACE)
add_library(libc::libc ALIAS libc_runtime)
target_link_libraries(libc_runtime INTERFACE c)
target_include_directories(libc_runtime INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(libc_runtime INTERFACE ${_libc_common_options})
target_link_options(libc_runtime INTERFACE ${_libc_link_options})
target_sources(libc_runtime INTERFACE $<TARGET_OBJECTS:libc_crt0>)

#
# libc_add_program(<name> <sources>...)
#
# An executable linked for the kernel (crt0 + user.ld + libc), plus
# <name>.bin, a flat binary of the same image for a loader that copies the
# file to the load address instead of reading ELF segments.
#
function(libc_add_program name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE libc::libc)
    add_dependencies(${name} libc_crt0)
    set_target_properties(${name} PROPERTIES
        POSITION_INDEPENDENT_CODE OFF
        LINK_DEPENDS ${LIBC_LINKER_SCRIPT})

    if(CMAKE_OBJCOPY)
        add_custom_command(TARGET ${name} POST_BUILD
            COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${name}> $<TARGET_FILE:${name}>.bin
            COMMENT "Flat binary ${name}.bin"
            VERBATIM)
    endif()
endfunction()

if(LIBC_BUILD_EXAMPLES)
    libc_add_program(hello examples/hello.c)
    libc_add_program(gfx_demo examples/gfx_demo.c)
    libc_add_program(clearance examples/clearance.c)
    libc_add_program(input_demo examples/input_demo.c)
    libc_add_program(sprite_demo examples/sprite_demo.c)
endif()

if(LIBC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()

