if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "TES3MP fuzzers require Clang and libFuzzer")
endif()

add_executable(tes3mp-packet-fuzz
    packet_fuzzer.cpp
)

target_link_libraries(tes3mp-packet-fuzz PRIVATE tes3mp-protocol-codec)
target_compile_options(tes3mp-packet-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
target_link_options(tes3mp-packet-fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
