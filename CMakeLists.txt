cmake_minimum_required(VERSION 3.20)
project(WeaponSoundEnhance LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(WeaponSoundEnhance SHARED
    WeaponSoundEnhance.cpp
)

target_include_directories(WeaponSoundEnhance PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_compile_definitions(WeaponSoundEnhance PRIVATE
    WIN32_LEAN_AND_MEAN NOMINMAX _WINDOWS _USRDLL
)

if(MSVC)
    target_compile_options(WeaponSoundEnhance PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(WeaponSoundEnhance PRIVATE -Wall -Wextra -Wpedantic)
endif()

# XAudio2 与系统库
target_link_libraries(WeaponSoundEnhance PRIVATE
    ole32 winmm xaudio2
)

set_target_properties(WeaponSoundEnhance PROPERTIES
    PREFIX ""
    OUTPUT_NAME "WeaponSoundEnhance"
)