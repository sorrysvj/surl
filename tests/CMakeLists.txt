add_executable(surl_tests
    test_main.cpp
    test_url.cpp
    test_parse.cpp
    test_mirror.cpp
    test_options.cpp
    test_integration.cpp
    fixture_server.cpp
)

target_link_libraries(surl_tests PRIVATE surl_core)
target_include_directories(surl_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

if(SURL_STATIC_RUNTIME AND MSVC)
    set_property(TARGET surl_tests PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_test(NAME surl_tests COMMAND surl_tests)

# The integration tests talk to a loopback fixture server; give them room on a
# loaded CI runner without letting a hang wedge the build forever.
set_tests_properties(surl_tests PROPERTIES TIMEOUT 300)
