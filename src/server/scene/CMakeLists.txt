include_directories(
  ${PROJECT_SOURCE_DIR}/include/renderers/gl
  ${PROJECT_SOURCE_DIR}/include/renderers/sw
)

ADD_LIBRARY(
  mirscene OBJECT

  application_session.cpp
  basic_surface.cpp
  broadcasting_session_event_sink.cpp
  default_configuration.cpp
        session_container.cpp
  mediating_display_changer.cpp
  session_manager.cpp
  surface_allocator.cpp
  surface_creation_parameters.cpp
  surface_stack.cpp
  surface_event_source.cpp
  null_surface_observer.cpp
  null_observer.cpp
  legacy_scene_change_notification.cpp
  legacy_surface_change_notification.cpp
  prompt_session_container.cpp
  prompt_session_impl.cpp
  prompt_session_manager_impl.cpp
  rendering_tracker.cpp
  default_coordinate_translator.cpp
  unsupported_coordinate_translator.cpp
  timeout_application_not_responding_detector.cpp
  output_properties_cache.cpp
  application_not_responding_detector_wrapper.cpp
  basic_clipboard.cpp
  surface_state_tracker.cpp
  ${CMAKE_SOURCE_DIR}/src/include/server/mir/scene/surface_observer.h
)

target_link_libraries(mirscene
  PUBLIC
    mirplatform
    mircommon
    mircore
)
