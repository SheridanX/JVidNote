add_subdirectory(lib)

add_executable(jvidnote_cli main.cpp)

target_link_libraries(jvidnote_cli PRIVATE audio_extractor)
