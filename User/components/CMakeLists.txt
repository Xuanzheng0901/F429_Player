file(GLOB_RECURSE COMPONENT_SOURCES
        ${CMAKE_CURRENT_LIST_DIR}/LOG/*.c
)

add_library(user_components INTERFACE)
target_sources(user_components INTERFACE ${COMPONENT_SOURCES})

target_include_directories(user_components INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/LOG
)
