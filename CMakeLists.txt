cmake_minimum_required(VERSION 3.12)

set(PICO_BOARD pico_w)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)

project(pico_freertos_samples)

# initialize the Pico SDK
pico_sdk_init()

add_compile_options(
  -Wall -Wno-format # int != int32_t as far as the compiler is concerned because
                    # gcc has int32_t as long int
  -Wno-unused-function # we have some for the docs that aren't called
)   

if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
  add_compile_options(-Wno-maybe-uninitialized)
endif()

add_subdirectory(freertos)
add_subdirectory(main_post)
add_subdirectory(main_get)
add_subdirectory(main_api)

