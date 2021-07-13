find_package(XKBCOMMON REQUIRED)

add_compile_definitions(MIR_LOG_COMPONENT_FALLBACK="mircommon")

set(MIR_COMMON_SOURCES)
set(MIR_COMMON_REFERENCES)

include_directories(
  ${MIR_GENERATED_INCLUDE_DIRECTORIES}
  ${PROJECT_SOURCE_DIR}/include/common
)

add_subdirectory(events)
add_subdirectory(logging)
add_subdirectory(report/lttng)
add_subdirectory(sharedlibrary)
add_subdirectory(thread)
add_subdirectory(time)
add_subdirectory(dispatch)

list(APPEND MIR_COMMON_SOURCES
  $<TARGET_OBJECTS:mirevents>
  $<TARGET_OBJECTS:mirtime>
  ${CMAKE_CURRENT_SOURCE_DIR}/output_type_names.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/log.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/libname.cpp
  ${PROJECT_SOURCE_DIR}/include/common/mir/libname.h
  ${PROJECT_SOURCE_DIR}/include/common/mir/posix_rw_mutex.h
  posix_rw_mutex.cpp
  edid.cpp
  event.cpp
  event_printer.cpp
  mir_cookie.cpp
  mir_cursor_api.cpp
)

set(
  MIR_GENERATED_INCLUDE_DIRECTORIES
  ${MIR_GENERATED_INCLUDE_DIRECTORIES}
  PARENT_SCOPE)

# TODO we need a place to manage ABI and related versioning but use this as placeholder
set(MIRCOMMON_ABI 8)
set(symbol_map ${CMAKE_CURRENT_SOURCE_DIR}/symbols.map)

add_library(mircommon SHARED
  input/mir_input_config.cpp
  input/mir_touchpad_config.cpp
  input/mir_pointer_config.cpp
  input/mir_keyboard_config.cpp
  input/mir_touchscreen_config.cpp
  input/input_event.cpp
  input/xkb_mapper.cpp
  input/parameter_keymap.cpp
  input/buffer_keymap.cpp
  ${PROJECT_SOURCE_DIR}/include/common/mir/input/mir_input_config.h
  ${PROJECT_SOURCE_DIR}/include/common/mir/input/mir_pointer_config.h
  ${PROJECT_SOURCE_DIR}/include/common/mir/input/mir_touchpad_config.h
  ${PROJECT_SOURCE_DIR}/include/common/mir/input/mir_touchscreen_config.h
  ${PROJECT_SOURCE_DIR}/include/common/mir/input/mir_keyboard_config.h
  ${MIR_COMMON_SOURCES}
)

target_include_directories(mircommon
  PUBLIC
    ${PROJECT_SOURCE_DIR}/include/common
)

set_source_files_properties(event.cpp PROPERTIES COMPILE_FLAGS
  "${CMAKE_CXXFLAGS} -I ${PROJECT_SOURCE_DIR}/src/include/cookie")

set_source_files_properties(input/input_event.cpp PROPERTIES COMPILE_FLAGS
  "${CMAKE_CXXFLAGS} -I ${PROJECT_SOURCE_DIR}/src/include/cookie")

target_link_libraries(mircommon
  PUBLIC
    mircookie
    mircore
    ${MIR_COMMON_REFERENCES}
    ${XKBCOMMON_LIBRARIES}
)


set_target_properties(mircommon

  PROPERTIES
  SOVERSION ${MIRCOMMON_ABI}
  LINK_FLAGS "-Wl,--exclude-libs=ALL -Wl,--version-script,${symbol_map}"
  LINK_DEPENDS ${symbol_map}
)

install(TARGETS mircommon LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(
  DIRECTORY ${CMAKE_SOURCE_DIR}/include/common/mir
  DESTINATION "include/mircommon"
)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/mircommon.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/mircommon.pc
    @ONLY
)
install(FILES       ${CMAKE_CURRENT_BINARY_DIR}/mircommon.pc    DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")
