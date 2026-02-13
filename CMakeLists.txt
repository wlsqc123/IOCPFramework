cmake_minimum_required(VERSION 3.20)

project(IOCPFramework
    VERSION 0.1.0
    LANGUAGES CXX
    DESCRIPTION "High-Performance MMORPG Game Server with IOCP"
)

# =============================================================================
# Global Configuration
# =============================================================================

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# MSVC specific settings
if(MSVC)
    add_compile_options(
        /W4                 # Warning level 4
        /permissive-        # Standards conformance
        /Zc:__cplusplus     # Correct __cplusplus macro
        /utf-8              # UTF-8 source and execution charset
        /MP                 # Multi-processor compilation
    )

    # Enable Address Sanitizer in Debug mode
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(/fsanitize=address)
    endif()

    # Optimization for Release
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /GL)
        add_link_options(/LTCG)
    endif()
endif()

# =============================================================================
# Subdirectories
# =============================================================================

add_subdirectory(common)
add_subdirectory(server)
add_subdirectory(client)

# =============================================================================
# Summary
# =============================================================================

message(STATUS "")
message(STATUS "========================================")
message(STATUS "IOCPFramework v${PROJECT_VERSION}")
message(STATUS "========================================")
message(STATUS "Build Type:      ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ Standard:    ${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler:        ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Install Prefix:  ${CMAKE_INSTALL_PREFIX}")
message(STATUS "========================================")
message(STATUS "")
