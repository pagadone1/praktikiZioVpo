cmake_minimum_required(VERSION 3.20)

project(ziovptrayui LANGUAGES CXX)

add_executable(ziovptrayui WIN32
    src/main.cpp
    resources/app.rc
)

target_compile_features(ziovptrayui PRIVATE cxx_std_17)
target_compile_definitions(ziovptrayui PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    target_compile_options(ziovptrayui PRIVATE /utf-8)
endif()

target_link_libraries(ziovptrayui PRIVATE user32 shell32 gdi32)

install(TARGETS ziovptrayui RUNTIME DESTINATION .)
