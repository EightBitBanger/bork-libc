# Host tests: the libc rebuilt with every syscall going through
# linux_shim.c, which emulates the kernel's syscalls on 32-bit Linux.
# Programs link exactly like real ones (crt0, user.ld, libc).

list(TRANSFORM _libc_sources PREPEND ${PROJECT_SOURCE_DIR}/ OUTPUT_VARIABLE _test_sources)
add_library(c_test STATIC ${_test_sources} linux_shim.c)
target_include_directories(c_test PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_compile_options(c_test PRIVATE ${_libc_common_options} ${_libc_build_options})
target_compile_definitions(c_test PUBLIC LIBC_SYSCALL_HOOK=__test_syscall)

function(libc_add_test_program name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE c_test)
    target_compile_options(${name} PRIVATE ${_libc_common_options} -Wno-format)
    target_link_options(${name} PRIVATE ${_libc_link_options})
    target_sources(${name} PRIVATE $<TARGET_OBJECTS:libc_crt0>)
    add_dependencies(${name} libc_crt0)
    set_target_properties(${name} PROPERTIES LINK_DEPENDS ${LIBC_LINKER_SCRIPT})
endfunction()

libc_add_test_program(run_test run_test.c)
libc_add_test_program(fmt_cases fmt_cases.c)

# Functional tests, in an empty scratch directory
add_test(NAME functional
    COMMAND ${CMAKE_COMMAND} -E env sh -c
        "rm -rf sandbox && mkdir sandbox && cd sandbox && \"$<TARGET_FILE:run_test>\"")

# Formatting identical to the host C library: the same cases built natively
find_program(LIBC_HOST_CC NAMES cc gcc clang)
if(LIBC_HOST_CC)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/fmt_cases_host
        COMMAND ${LIBC_HOST_CC} -O2 -w ${CMAKE_CURRENT_SOURCE_DIR}/fmt_cases.c -o fmt_cases_host
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/fmt_cases.c
        COMMENT "Host reference build of fmt_cases"
        VERBATIM)
    add_custom_target(fmt_cases_host_build ALL DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/fmt_cases_host)

    add_test(NAME printf_matches_host
        COMMAND sh -c "\"$<TARGET_FILE:fmt_cases>\" > ours.txt && ./fmt_cases_host > host.txt && diff -u host.txt ours.txt")
endif()
