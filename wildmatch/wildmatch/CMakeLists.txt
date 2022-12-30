add_library(wildmatch SHARED wildmatch.c)
target_compile_options(wildmatch PRIVATE ${PROJECT_CFLAGS})

add_library(wildmatch-cxx SHARED wildmatch.cpp)
target_compile_options(wildmatch-cxx PRIVATE ${PROJECT_CXXFLAGS})

install(TARGETS wildmatch wildmatch-cxx DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES wildmatch.h DESTINATION include/${PROJECT_NAME})
install(FILES wildmatch.hpp DESTINATION include/${PROJECT_NAME})
