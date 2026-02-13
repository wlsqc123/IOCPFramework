# =============================================================================
# Engine Library
# =============================================================================

# Collect all engine source files
file(GLOB_RECURSE ENGINE_SOURCES
    "iocp/*.cpp"
    "session/*.cpp"
    "buffer/*.cpp"
    "packet/*.cpp"
    "coroutine/*.cpp"
    "timer/*.cpp"
)

file(GLOB_RECURSE ENGINE_HEADERS
    "iocp/*.h"
    "session/*.h"
    "buffer/*.h"
    "packet/*.h"
    "coroutine/*.h"
    "timer/*.h"
)

# Create engine library
add_library(engine STATIC
    ${ENGINE_SOURCES}
    ${ENGINE_HEADERS}
)

target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(engine PUBLIC
    common
    ws2_32      # Winsock2
    mswsock     # Microsoft Winsock extensions
)

# C++20 features
target_compile_features(engine PUBLIC cxx_std_20)

# =============================================================================
# Main Executable
# =============================================================================

add_executable(Server main.cpp)
target_link_libraries(Server PRIVATE engine)
