add_library(common)
target_include_directories(common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(common PUBLIC cxx_std_20)
target_sources(common PUBLIC FILE_SET CXX_MODULES FILES Types.ixx)
