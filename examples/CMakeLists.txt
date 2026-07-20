# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(iridium_example)

# After each app link, patch compile_commands.json so clangd can resolve
# toolchain newlib headers (e.g. sys/reent.h) without --query-driver.
set(_CLANGD_PATCH "${CMAKE_CURRENT_LIST_DIR}/../scripts/patch_compile_commands_for_clangd.py")
if(EXISTS "${_CLANGD_PATCH}")
    idf_build_get_property(_idf_python PYTHON)
    add_custom_command(
        TARGET ${CMAKE_PROJECT_NAME}.elf
        POST_BUILD
        COMMAND "${_idf_python}" "${_CLANGD_PATCH}" "${CMAKE_BINARY_DIR}/compile_commands.json"
        COMMENT "Patching compile_commands.json for clangd newlib includes"
        VERBATIM
    )
endif()
