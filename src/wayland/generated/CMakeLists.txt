
get_filename_component(
        PROTOCOL_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../protocol/
        ABSOLUTE
        BASE_DIR ${PROJECT_SOURCE_DIR}
)

get_filename_component(
        GENERATED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/
        ABSOLUTE
        BASE_DIR ${PROJECT_SOURCE_DIR}
)

set(GENERATED_FILES "")

macro(GENERATE_PROTOCOL NAME_PREFIX PROTOCOL_NAME)
    set(PROTOCOL_PATH "${PROTOCOL_DIR}/${PROTOCOL_NAME}.xml")
    set(OUTPUT_PATH_HEADER "${GENERATED_DIR}/${PROTOCOL_NAME}_wrapper.h")
    set(OUTPUT_PATH_SRC "${GENERATED_DIR}/${PROTOCOL_NAME}_wrapper.cpp")
    add_custom_command(OUTPUT "${OUTPUT_PATH_HEADER}"
            VERBATIM
            COMMAND "sh" "-c" "${CMAKE_BINARY_DIR}/bin/mir_wayland_generator ${NAME_PREFIX} ${PROTOCOL_PATH} header > ${OUTPUT_PATH_HEADER}"
            DEPENDS "${PROTOCOL_PATH}"
            DEPENDS mir_wayland_generator
            )
    add_custom_command(OUTPUT "${OUTPUT_PATH_SRC}"
            VERBATIM
            COMMAND "sh" "-c" "${CMAKE_BINARY_DIR}/bin/mir_wayland_generator ${NAME_PREFIX} ${PROTOCOL_PATH} source > ${OUTPUT_PATH_SRC}"
            DEPENDS "${PROTOCOL_PATH}"
            DEPENDS mir_wayland_generator
            )
    set(GENERATED_FILES ${GENERATED_FILES} "${OUTPUT_PATH_HEADER}" "${OUTPUT_PATH_SRC}")
endmacro()

GENERATE_PROTOCOL("wl_" "wayland")
GENERATE_PROTOCOL("z" "xdg-shell-unstable-v6")
GENERATE_PROTOCOL("_" "xdg-shell") # empty prefix is not allowed, but '_' won't match anything, so it is ignored
GENERATE_PROTOCOL("z" "xdg-output-unstable-v1")
GENERATE_PROTOCOL("zwlr_" "wlr-layer-shell-unstable-v1")
GENERATE_PROTOCOL("zwlr_" "wlr-foreign-toplevel-management-unstable-v1")
GENERATE_PROTOCOL("zwp_" "pointer-constraints-unstable-v1")
GENERATE_PROTOCOL("zwp_" "relative-pointer-unstable-v1")
GENERATE_PROTOCOL("zwp_" "virtual-keyboard-unstable-v1")

add_custom_target(refresh-wayland-wrapper
    DEPENDS ${GENERATED_FILES}
    DEPENDS mir_wayland_generator
    SOURCES ${GENERATED_FILES}
)

set_directory_properties(PROPERTIES CLEAN_NO_CUSTOM 1)

set(GENERATED_FILES ${GENERATED_FILES} PARENT_SCOPE)