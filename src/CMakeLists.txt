# We need MIRPLATFORM_ABI in both libmirplatform and the platform implementations.
set(MIRPLATFORM_ABI 23)

set(MIRAL_VERSION_MAJOR 3)
set(MIRAL_VERSION_MINOR 3)
set(MIRAL_VERSION_PATCH 0)
set(MIRAL_VERSION ${MIRAL_VERSION_MAJOR}.${MIRAL_VERSION_MINOR}.${MIRAL_VERSION_PATCH})

# Add the cookie implementation before exposing any APIs
add_subdirectory(cookie/)

# Add the core and platform implementations before exposing any private APIs
add_subdirectory(core)
add_subdirectory(platforms/)

# the src/include/... directories should be private to the implementation
include_directories(${PROJECT_SOURCE_DIR}/src/include/common)
set(MIR_GENERATED_INCLUDE_DIRECTORIES)
add_subdirectory(common/)
include_directories(${MIR_GENERATED_INCLUDE_DIRECTORIES})

add_subdirectory(platform/)
add_subdirectory(wayland/)
add_subdirectory(server/)
add_subdirectory(miral/)
add_subdirectory(miroil/)
add_subdirectory(renderer/)
add_subdirectory(renderers/)
add_subdirectory(gl/)

add_executable(wrapper wrapper.c)
target_compile_definitions(wrapper PUBLIC
    EXECUTABLE_FORMAT=\"%s.bin\"
    _DEFAULT_SOURCE
    _BSD_SOURCE)

set(
  MIR_GENERATED_INCLUDE_DIRECTORIES
  ${MIR_GENERATED_INCLUDE_DIRECTORIES}
  PARENT_SCOPE)

set(MIR_SERVER_OBJECTS ${MIR_SERVER_OBJECTS} PARENT_SCOPE)
set(MIR_SERVER_REFERENCES ${MIR_SERVER_REFERENCES} PARENT_SCOPE)
set(MIR_PLATFORM_OBJECTS ${MIR_PLATFORM_OBJECTS} PARENT_SCOPE)
set(MIR_PLATFORM_REFERENCES ${MIR_PLATFORM_REFERENCES} PARENT_SCOPE)
set(MIR_SERVER_PLATFORM_PATH ${MIR_SERVER_PLATFORM_PATH} PARENT_SCOPE)
set(MIRSERVER_INCLUDE_DIRS ${MIRSERVER_INCLUDE_DIRS} PARENT_SCOPE)

# We need the ABI versions in the tests
set(MIR_SERVER_GRAPHICS_PLATFORM_ABI ${MIR_SERVER_GRAPHICS_PLATFORM_ABI} PARENT_SCOPE)
set(MIR_SERVER_INPUT_PLATFORM_ABI ${MIR_SERVER_INPUT_PLATFORM_ABI} PARENT_SCOPE)
set(MIR_SERVER_GRAPHICS_PLATFORM_VERSION ${MIR_SERVER_GRAPHICS_PLATFORM_VERSION} PARENT_SCOPE)
set(MIR_INPUT_PLATFORM_VERSION_SCRIPT ${MIR_INPUT_PLATFORM_VERSION_SCRIPT} PARENT_SCOPE)

