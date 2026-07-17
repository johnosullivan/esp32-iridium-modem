idf_component_register(
    SRCS
        "iridium_example_main.c"
        "led_strip_encoder.c"
        "../../stack.c"
        "../../iridium.c"
        "../../iridium_parser.c"
    INCLUDE_DIRS
        "."
        "../.."
)

# xtensa-*-gcc defines __XTENSA__ as a builtin, but compile_commands.json does not
# record builtins. clangd then takes IDF's host fallback `#include "../hal.h"` and
# reports a false missing-header error on FreeRTOS.h. Export the define explicitly.
if(CONFIG_IDF_TARGET_ARCH_XTENSA)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC __XTENSA__ __xtensa__)
endif()
