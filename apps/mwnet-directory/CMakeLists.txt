add_executable(mwnet-directory main.cpp)
target_link_libraries(mwnet-directory PRIVATE mwnet-directory-core)
install(TARGETS mwnet-directory RUNTIME DESTINATION bin)
