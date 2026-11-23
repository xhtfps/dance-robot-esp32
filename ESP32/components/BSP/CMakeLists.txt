set(src_dirs "")
set(include_dirs "")

file(GLOB led_sources "${CMAKE_CURRENT_LIST_DIR}/LED/*.c")
if(led_sources)
    list(APPEND src_dirs LED)
    list(APPEND include_dirs LED)
endif()

file(GLOB camera_sources "${CMAKE_CURRENT_LIST_DIR}/CAMERA/*.c")
if(camera_sources)
    list(APPEND src_dirs CAMERA)
    list(APPEND include_dirs CAMERA)
endif()

file(GLOB pose_sources
    "${CMAKE_CURRENT_LIST_DIR}/POSE/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/POSE/*.cpp")
if(pose_sources)
    list(APPEND src_dirs POSE)
    list(APPEND include_dirs POSE)
endif()

file(GLOB face_display_sources "${CMAKE_CURRENT_LIST_DIR}/FACE_DISPLAY/*.c")
if(face_display_sources)
    list(APPEND src_dirs FACE_DISPLAY)
    list(APPEND include_dirs FACE_DISPLAY)
endif()

file(GLOB audio_i2s_sources "${CMAKE_CURRENT_LIST_DIR}/AUDIO_I2S/*.c")
if(audio_i2s_sources)
    list(APPEND src_dirs AUDIO_I2S)
    list(APPEND include_dirs AUDIO_I2S)
endif()

file(GLOB serial_bridge_sources "${CMAKE_CURRENT_LIST_DIR}/SERIAL_BRIDGE/*.c")
if(serial_bridge_sources)
    list(APPEND src_dirs SERIAL_BRIDGE)
    list(APPEND include_dirs SERIAL_BRIDGE)
endif()

file(GLOB competition_sources "${CMAKE_CURRENT_LIST_DIR}/COMPETITION/*.c")
if(competition_sources)
    list(APPEND src_dirs COMPETITION)
    list(APPEND include_dirs COMPETITION)
endif()

file(GLOB music_sources "${CMAKE_CURRENT_LIST_DIR}/MUSIC/*.c")
if(music_sources)
    list(APPEND src_dirs MUSIC)
    list(APPEND include_dirs MUSIC)
endif()

file(GLOB wifi_sources "${CMAKE_CURRENT_LIST_DIR}/WIFI/*.c")
if(wifi_sources)
    list(APPEND src_dirs WIFI)
    list(APPEND include_dirs WIFI)
endif()

file(GLOB web_sources "${CMAKE_CURRENT_LIST_DIR}/WEB/*.c")
if(web_sources)
    list(APPEND src_dirs WEB)
    list(APPEND include_dirs WEB)
endif()

idf_component_register(
    SRC_DIRS ${src_dirs}
    INCLUDE_DIRS ${include_dirs}
    REQUIRES driver esp_driver_spi esp_driver_i2s led_strip esp32-camera esp_jpeg coco_pose
             esp_wifi esp_event esp_netif esp_http_server nvs_flash json
)
