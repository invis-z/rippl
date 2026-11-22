set(RIPPL_BUILTIN_THEMES dark light fjord dusk neon phosphor paper olive high_contrast)
set(RIPPL_BUILTIN_THEME_FILES "")
set(builtin_arrays "")
set(builtin_entries "")
foreach(theme ${RIPPL_BUILTIN_THEMES})
    set(theme_file ${CMAKE_CURRENT_SOURCE_DIR}/packs/${theme}.rippltheme)
    if(EXISTS ${theme_file})
        list(APPEND RIPPL_BUILTIN_THEME_FILES ${theme_file})
        file(READ ${theme_file} theme_hex HEX)
        string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," theme_bytes "${theme_hex}")
        string(APPEND builtin_arrays "static const uint8_t THEME_${theme}[] = {${theme_bytes}};\n")
        string(APPEND builtin_entries "    {THEME_${theme}, sizeof THEME_${theme}},\n")
    else()
        string(APPEND builtin_entries "    {0, 0},\n")
    endif()
endforeach()
list(LENGTH RIPPL_BUILTIN_THEMES builtin_count)
set(RIPPL_BUILTIN_THEME_FILES "${RIPPL_BUILTIN_THEME_FILES}" CACHE INTERNAL "built-in themes in theme order")
set_property(DIRECTORY ${PROJECT_SOURCE_DIR} APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RIPPL_BUILTIN_THEME_FILES})

file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/builtin_themes.c.new
"#include \"themes/builtin_themes.h\"\n\n${builtin_arrays}\n"
"const BuiltinTheme BUILTIN_THEMES[BUILTIN_THEME_COUNT] = {\n${builtin_entries}};\n\n"
"_Static_assert(${builtin_count} == BUILTIN_THEME_COUNT, \"one built-in theme for each theme setting\");\n")
configure_file(${CMAKE_CURRENT_BINARY_DIR}/builtin_themes.c.new ${CMAKE_CURRENT_BINARY_DIR}/builtin_themes.c COPYONLY)

add_library(rippl_builtin_themes STATIC EXCLUDE_FROM_ALL ${CMAKE_CURRENT_BINARY_DIR}/builtin_themes.c)
target_include_directories(rippl_builtin_themes PUBLIC include)
target_link_libraries(rippl_builtin_themes PUBLIC rippl_settings)
