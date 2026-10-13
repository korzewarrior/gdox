if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_library(gdox_disc_bridge STATIC
        src/platform/disc_bridge_linux.c
        src/platform/disc_bridge_tools_linux.c)
    add_library(gdox::disc_bridge ALIAS gdox_disc_bridge)
    target_include_directories(gdox_disc_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(gdox_disc_bridge PUBLIC gdox::core gdox::platform)
    gdox_enable_c_warnings(gdox_disc_bridge)
endif()
