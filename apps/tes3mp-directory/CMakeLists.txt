add_executable(tes3mp-directory main.cpp)
target_link_libraries(tes3mp-directory PRIVATE tes3mp-directory-core)
install(TARGETS tes3mp-directory RUNTIME DESTINATION bin)
