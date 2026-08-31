cmake_minimum_required(VERSION 3.24)
project(MarioFundamentalsNative VERSION 2.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

configure_file(src/resources.rc.in ${CMAKE_CURRENT_BINARY_DIR}/resources.rc @ONLY)
set_property(SOURCE ${CMAKE_CURRENT_BINARY_DIR}/resources.rc PROPERTY OBJECT_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/app.ico;${CMAKE_CURRENT_SOURCE_DIR}/assets/title.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/credits.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/startup/brainstorm.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/startup/brainstorm-fade.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/startup/stepping-stone.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/startup/stepping-stone-fade.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/backgammon-1.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/backgammon-2.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/dominoes-1.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/gofish-1.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/gofish-2.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/yacht-1.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/help/yacht-2.bmp;${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/times-14.nfnt;${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/geneva-9.nfnt;${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/monaco-12.nfnt;${CMAKE_CURRENT_SOURCE_DIR}/assets/MarioFundamentals.pack;${CMAKE_CURRENT_SOURCE_DIR}/assets/MarioGameGallery.pack;${CMAKE_CURRENT_SOURCE_DIR}/src/app.manifest"
)

add_executable(MarioFundamentals WIN32
    src/main.cpp
    src/launcher.cpp
    src/dos_app.cpp
    src/dos_help_overlay.cpp
    src/app.cpp
    src/asset_store.cpp
    src/pak.cpp
    src/movie.cpp
    src/canvas.cpp
    src/source_fonts.cpp
    src/about.cpp
    src/audio.cpp
    src/games/backgammon.cpp
    src/games/checkers.cpp
    src/games/dominoes.cpp
    src/games/go_fish.cpp
    src/games/yacht.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/resources.rc
)

target_include_directories(MarioFundamentals PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR}/src>
)
target_compile_definitions(MarioFundamentals PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(MarioFundamentals PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(MarioFundamentals PRIVATE user32 gdi32 winmm comctl32 advapi32)

if(MINGW)
    target_link_options(MarioFundamentals PRIVATE
        -municode
        -static
        -static-libgcc
        -static-libstdc++
        -Wl,--no-insert-timestamp
    )
endif()

set_target_properties(MarioFundamentals PROPERTIES
    OUTPUT_NAME "MarioFundamentals"
)

install(TARGETS MarioFundamentals RUNTIME DESTINATION .)
