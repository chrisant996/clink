add_executable(test-wildmatch test-wildmatch.c)
target_link_libraries(test-wildmatch wildmatch)
set_target_properties(test-wildmatch PROPERTIES RUNTIME_OUTPUT_DIRECTORY
                      ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(test-wildmatch PRIVATE ${PROJECT_CFLAGS})

add_test(git-wildmatch-tests ${CMAKE_CURRENT_SOURCE_DIR}/t3070-wildmatch.sh)
