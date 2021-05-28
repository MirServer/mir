include_directories(../frontend_xwayland ${CMAKE_CURRENT_BINARY_DIR})

# One day, maybe, we can add include dependences to an OBJECT library. Until then...
get_property(mirwayland_includes TARGET mirwayland PROPERTY INTERFACE_INCLUDE_DIRECTORIES)
include_directories(${mirwayland_includes})

set(
  WAYLAND_SOURCES

  mir_display.cpp               mir_display.h
  wayland_default_configuration.cpp
  wayland_connector.cpp         wayland_connector.h
  wl_client.cpp                 wl_client.h
  wayland_executor.cpp          wayland_executor.h
  null_event_sink.cpp           null_event_sink.h
  wayland_surface_observer.cpp  wayland_surface_observer.h
  wayland_input_dispatcher.cpp  wayland_input_dispatcher.h
  wl_data_device_manager.cpp    wl_data_device_manager.h
  wl_data_device.cpp            wl_data_device.h
  wl_data_source.cpp            wl_data_source.h
  output_manager.cpp            output_manager.h
  pointer_constraints_unstable_v1.cpp pointer_constraints_unstable_v1.h
  relative_pointer_unstable_v1.cpp    relative_pointer_unstable_v1.h
  wl_subcompositor.cpp          wl_subcompositor.h
                                wl_surface_role.h
  window_wl_surface_role.cpp    window_wl_surface_role.h
  wl_surface.cpp                wl_surface.h
  wl_seat.cpp                   wl_seat.h
  wl_keyboard.cpp               wl_keyboard.h
  wl_pointer.cpp                wl_pointer.h
  wl_touch.cpp                  wl_touch.h
  xdg_shell_v6.cpp              xdg_shell_v6.h
  xdg_shell_stable.cpp          xdg_shell_stable.h
  xdg_output_v1.cpp             xdg_output_v1.h
  layer_shell_v1.cpp            layer_shell_v1.h
  deleted_for_resource.cpp      deleted_for_resource.h
  wl_region.cpp                 wl_region.h
  foreign_toplevel_manager_v1.cpp foreign_toplevel_manager_v1.h
  frame_executor.cpp            frame_executor.h
  virtual_keyboard_v1.cpp       virtual_keyboard_v1.h
  ${PROJECT_SOURCE_DIR}/src/include/server/mir/frontend/wayland.h
  ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.c
  ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.h
)

add_custom_command(
  OUTPUT
    ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.c
    ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.h
  COMMAND
    lttng-gen-tp
        ${CMAKE_CURRENT_SOURCE_DIR}/wayland_frontend.tp
        -o wayland_frontend.tp.h
        -o wayland_frontend.tp.c
  WORKING_DIRECTORY
    ${CMAKE_CURRENT_BINARY_DIR}
  DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/wayland_frontend.tp
)

# Inconveniently, GCC on 16.04 hits an ICE when attempting to use LTO
# on the tracepoints. Fortunately we can turn it off for just that translation
# unit.
if (CMAKE_COMPILER_IS_GNUCXX AND (CMAKE_CXX_COMPILER_VERSION VERSION_LESS 6))
  set(TRACEPOINT_COMPILE_FLAGS "-fno-lto")
endif()

check_cxx_compiler_flag(-Wgnu-empty-initializer HAS_W_GNU_EMPTY_INITIALIZER)
if (HAS_W_GNU_EMPTY_INITIALIZER)
  set(TRACEPOINT_COMPILE_FLAGS "${TRACEPOINT_COMPILE_FLAGS} -Wno-error=gnu-empty-initializer")
endif()

set_source_files_properties(
        ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.c
        ${CMAKE_CURRENT_BINARY_DIR}/wayland_frontend.tp.h
        PROPERTIES
        COMPILE_FLAGS "${TRACEPOINT_COMPILE_FLAGS}"
)

add_library(mirfrontend-wayland OBJECT

  ${WAYLAND_SOURCES}
)

target_link_libraries(mirfrontend-wayland
  PUBLIC
    mirplatform
    mircommon
    mircore
)

