add_executable(rippl_preview
    preview.c
    libc/libc.c
    ${PROJECT_SOURCE_DIR}/tests/app/fake_hal.c
)
set_target_properties(rippl_preview PROPERTIES SUFFIX .wasm)
set_source_files_properties(libc/libc.c PROPERTIES COMPILE_OPTIONS -fno-builtin)
target_include_directories(rippl_preview PRIVATE
    libc/include
    ${PROJECT_SOURCE_DIR}/tests/app
    ${PROJECT_SOURCE_DIR}/src/app
)
target_link_libraries(rippl_preview PRIVATE rippl_app rippl_builtin_themes rippl_library_scan rippl_options)
target_link_options(rippl_preview PRIVATE -Wl,-z,stack-size=262144 -Wl,--export=__heap_base)
