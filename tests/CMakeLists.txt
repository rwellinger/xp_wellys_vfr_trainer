# Catch2 unit tests for the SDK-free engine. Reuses the xp_trainer_engine
# OBJECT library so we test the production code, not a re-implementation.
# Never links the X-Plane SDK — anything that needs XPLM belongs in the
# plugin target.

set(CATCH2_DIR ${CMAKE_SOURCE_DIR}/vendor/catch2)

add_executable(xp_wellys_vfr_trainer_tests
    $<TARGET_OBJECTS:xp_trainer_engine>
    ${CATCH2_DIR}/catch_amalgamated.cpp
    test_smoke.cpp
    test_backends.cpp
    test_airports.cpp
    test_preflight.cpp
)

target_include_directories(xp_wellys_vfr_trainer_tests PRIVATE
    ${CATCH2_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/vendor
)

# Lets test_airports.cpp open the real apt.dat excerpt fixture without a
# hard-coded absolute path.
target_compile_definitions(xp_wellys_vfr_trainer_tests PRIVATE
    TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testdata"
)

# manager.cpp (in the engine OBJECT lib) calls curl_global_init/cleanup, so
# the test executable links libcurl even though the mock backend never
# performs an HTTP transfer.
target_link_libraries(xp_wellys_vfr_trainer_tests PRIVATE CURL::libcurl)

if(APPLE)
    target_compile_options(xp_wellys_vfr_trainer_tests PRIVATE
        -Wall -Wextra -Wno-unused-parameter
    )
endif()

set_target_properties(xp_wellys_vfr_trainer_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
