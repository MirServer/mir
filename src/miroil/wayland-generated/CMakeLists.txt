set(PROTOCOL "mir-trusted-prompt-sessions-v1")
set(PROTOCOL_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../wayland-protocols/${PROTOCOL}.xml")
set(GENERATE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/${PROTOCOL}_wrapper")

add_custom_command(
    OUTPUT ${GENERATE_FILE}.cpp
    OUTPUT ${GENERATE_FILE}.h
    DEPENDS ${PROTOCOL_FILE}
    COMMAND "sh" "-c" "${CMAKE_BINARY_DIR}/bin/mir_wayland_generator _ ${PROTOCOL_FILE} header >${GENERATE_FILE}.h"
    COMMAND "sh" "-c" "${CMAKE_BINARY_DIR}/bin/mir_wayland_generator _ ${PROTOCOL_FILE} source >${GENERATE_FILE}.cpp"
)

add_custom_target(mir-trusted-prompt-sessions
    DEPENDS ${GENERATE_FILE}.cpp
    DEPENDS ${GENERATE_FILE}.h
    SOURCES
        ${GENERATE_FILE}.cpp
        ${GENERATE_FILE}.h
)

set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)
