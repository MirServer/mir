set(MIROIL_ABI 1)
set(MIROIL_VERSION_MAJOR ${MIROIL_ABI})
set(MIROIL_VERSION_MINOR 0)
set(MIROIL_VERSION_PATCH 0)
set(MIROIL_VERSION ${MIROIL_VERSION_MAJOR}.${MIROIL_VERSION_MINOR}.${MIROIL_VERSION_PATCH})


set(symbol_map ${CMAKE_CURRENT_SOURCE_DIR}/symbols.map)
set(miroil_include ${PROJECT_SOURCE_DIR}/include/miroil)

add_definitions(-DMIR_LOG_COMPONENT_FALLBACK="miroil" -DMIROIL_ENABLE_DEPRECATIONS=0)

add_subdirectory(wayland-generated)

add_library(miroil-internal STATIC
    wayland-generated/mir-trusted-prompt-sessions-v1_wrapper.cpp
    wayland-generated/mir-trusted-prompt-sessions-v1_wrapper.h
)

set_target_properties(miroil-internal
        PROPERTIES COMPILE_FLAGS "${CMAKE_CXXFLAGS}  -fvisibility=hidden")

target_include_directories(miroil-internal
    PRIVATE ${MIRSERVER_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include/wayland
)

target_link_libraries(miroil-internal
    PRIVATE
        mircommon
)

add_library(miroil SHARED
    compositor.cpp ${miroil_include}/miroil/compositor.h    
    edid.cpp ${miroil_include}/miroil/edid.h
    mirbuffer.cpp ${miroil_include}/miroil/mirbuffer.h
    persist_display_config.cpp ${miroil_include}/miroil/persist_display_config.h
    display_configuration_policy.cpp ${miroil_include}/miroil/display_configuration_policy.h
    eventdispatch.cpp ${miroil_include}/miroil/eventdispatch.h
    event_builder.cpp ${miroil_include}/miroil/event_builder.h
    input_device.cpp ${miroil_include}/miroil/input_device.h
    input_device_observer.cpp ${miroil_include}/miroil/input_device_observer.h
    mir_server_hooks.cpp ${miroil_include}/miroil/mir_server_hooks.h
    open_gl_context.cpp ${miroil_include}/miroil/open_gl_context.h    
    prompt_session_listener.cpp ${miroil_include}/miroil/prompt_session_listener.h
    prompt_session_manager.cpp ${miroil_include}/miroil/prompt_session_manager.h    
    set_compositor.cpp ${miroil_include}/miroil/set_compositor.h
    ${miroil_include}/miroil/display_configuration_storage.h
    ${miroil_include}/miroil/display_id.h
)

target_include_directories(miroil
    PUBLIC  "${miroil_include}"
    PRIVATE ${MIRSERVER_INCLUDE_DIRS}  ${MIRCLIENT_INCLUDE_DIRS}
    ${PROJECT_SOURCE_DIR}/include/platform
    ${PROJECT_SOURCE_DIR}/src/include/gl
    ${PROJECT_SOURCE_DIR}/include/renderers/gl
    ${PROJECT_SOURCE_DIR}/include/server
    ${PROJECT_SOURCE_DIR}/include/client
    ${PROJECT_SOURCE_DIR}/include/miral
    ${PROJECT_SOURCE_DIR}/src/include/server
)

target_link_libraries(miroil
    PRIVATE
        miroil-internal
        mircommon
        mirserver
        miral
)

set_target_properties(miroil
        PROPERTIES
        SOVERSION ${MIROIL_ABI}
        LINK_FLAGS "-Wl,--exclude-libs=ALL -Wl,--version-script,${symbol_map}"
        LINK_DEPENDS ${symbol_map}
)

if (CMAKE_COMPILER_IS_GNUCXX   AND  # clang generates slightly different symbols (but we don't care)
    MIR_LINK_TIME_OPTIMIZATION AND  # g++ generates slightly different symbols without LTO (but we don't care)
    EXISTS /etc/debian_version)     # Using dpkg-gensymbols only makes sense on Debian based distros

    find_program(MIR_DPKG_GENSYMBOLS dpkg-gensymbols)
    if (MIR_DPKG_GENSYMBOLS)
        add_custom_target(check-miroil-symbols ALL
            DEPENDS miroil ${PROJECT_SOURCE_DIR}/debian/libmiroil${MIROIL_ABI}.symbols
            COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/check-and-update-debian-symbols.py ${CMAKE_LIBRARY_OUTPUT_DIRECTORY} miroil 2.4.0 ${MIROIL_ABI} ${MIROIL_VERSION_MINOR}
            WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
            VERBATIM)
    endif()
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/miroil.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/miroil.pc
    @ONLY
)

if(TARGET doc)
    add_custom_target(regenerate-miroil-symbols-map
            ${CMAKE_CURRENT_SOURCE_DIR}/regenerate-miroil-symbols-map.py ${CMAKE_BINARY_DIR}/doc/xml/*.xml > ${symbol_map}
            DEPENDS doc)
endif()

install(TARGETS     miroil                           LIBRARY DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}")
install(DIRECTORY   ${CMAKE_SOURCE_DIR}/include/miroil       DESTINATION "${CMAKE_INSTALL_PREFIX}/include")
install(FILES       ${CMAKE_CURRENT_BINARY_DIR}/miroil.pc    DESTINATION "${CMAKE_INSTALL_FULL_LIBDIR}/pkgconfig")
