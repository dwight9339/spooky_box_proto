# Included by both maintained core projects, never by CubeMX source lists.
# Demo-only behavior (decision 0011) exists only on demo/halloween-2026 and
# compiles only when SPOOKY_DEMO is ON, which the Demo preset sets.
option(SPOOKY_DEMO
    "Build the Halloween 2026 demo-only behavior (decision 0011, demo branch only)" OFF)
if(SPOOKY_DEMO)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE SPOOKY_DEMO=1)
endif()
