cmake_minimum_required(VERSION 3.28.2)

include(../sdk/config.cmake)
project(sieelf_sie_mpctl C)

include_directories("../libsie/src/include")
link_libraries(crt crt_helper c gcc ft sie)
file(GLOB_RECURSE SOURCES "src/*.c")

add_executable(SieMPCtl_NSG ${SOURCES})
target_sdk_setup(SieMPCtl_NSG NEWSGOLD)
target_link_directories(SieMPCtl_NSG PUBLIC "../libsie/build/lib/NSG")
target_compile_options(SieMPCtl_NSG BEFORE PUBLIC -std=gnu99 -Wall -Werror)
