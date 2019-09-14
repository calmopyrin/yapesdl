cmake_minimum_required(VERSION 3.2)
set(CMAKE_TOOLCHAIN_FILE $ENV{WUT_ROOT}/share/wut.toolchain.cmake)

set(PROJECT_NAME
    yapesdl)

project(${PROJECT_NAME})

set(LIBRARIES
    /opt/devkitpro/portlibs/ppc/lib
    /opt/devkitpro/portlibs/wiiu/lib
    /opt/devkitpro/wut/lib
)

set(INCLUDES
    /opt/devkitpro/portlibs/wiiu/include
    /opt/devkitpro/portlibs/ppc/include
    /opt/devkitpro/wut/include
)

set(LINK_NAMES
    SDL2_ttf
    SDL2_mixer
    SDL2_gfx
    SDL2_image 
    SDL2
    freetype
    png
    mpg123
    jpeg
    z
    m
    wut)

file(GLOB SRCS
    *.h*
    *.c*
)

include_directories(${INCLUDES})
link_directories(${LIBRARIES} REQUIRED)

include("${WUT_ROOT}/share/wut.cmake" REQUIRED)

add_executable(${PROJECT_NAME}.elf
    ${SRCS}
)

target_compile_options(${PROJECT_NAME}.elf PRIVATE -g -Wall -O2 -ffast-math )

target_link_libraries(${PROJECT_NAME}.elf
    ${LINK_NAMES})

wut_create_rpx(${PROJECT_NAME}.rpx ${PROJECT_NAME}.elf)