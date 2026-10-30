add_executable(mwnet-directory main.cpp)
target_link_libraries(mwnet-directory PRIVATE mwnet-directory-core)
# The desktop macOS bundles do not relocate the backend's Homebrew libraries.
# Keep this operator backend out of the experimental desktop package.
if(NOT APPLE)
    install(TARGETS mwnet-directory RUNTIME DESTINATION bin)
endif()
