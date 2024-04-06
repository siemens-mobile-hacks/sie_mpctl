cmake_minimum_required(VERSION 3.28.2)

include(../sdk/config.cmake)
project(sieelf_sie_mpctl C)

include_directories("../libsie/src/include")
link_libraries(crt crt_helper c gcc ft sie)
file(GLOB_RECURSE SOURCES "src/*.c")

add_executable(sie_mpctl ${SOURCES})
target_sdk_setup(sie_mpctl NEWSGOLD)
target_link_directories(sie_mpctl PUBLIC "../libsie/build/lib/NSG")
target_compile_options(sie_mpctl BEFORE PUBLIC -std=gnu99 -Wall -Werror)
