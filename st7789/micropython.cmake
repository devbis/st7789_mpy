add_library(usermod_st7789 INTERFACE)

target_sources(usermod_st7789 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/st7789.c
)

target_include_directories(usermod_st7789 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}
)

target_link_libraries(usermod INTERFACE usermod_st7789)
