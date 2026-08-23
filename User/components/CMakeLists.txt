file(GLOB_RECURSE COMPONENT_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/LOG/*.c
        ${CMAKE_CURRENT_LIST_DIR}/SD_CARD/*.c
)

add_library(user_components INTERFACE)
target_sources(user_components INTERFACE ${COMPONENT_SOURCES})

target_include_directories(user_components INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/LOG
        ${CMAKE_CURRENT_LIST_DIR}/SD_CARD
)
