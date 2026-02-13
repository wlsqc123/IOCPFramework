add_executable(Client main.cpp)

target_link_libraries(Client PRIVATE
    common
    ws2_32
)

target_compile_features(Client PRIVATE cxx_std_20)
