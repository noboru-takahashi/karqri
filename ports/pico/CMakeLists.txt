cmake_minimum_required(VERSION 3.13)

if (NOT DEFINED PICO_SDK_PATH)
    if (DEFINED ENV{PICO_SDK_PATH})
        set(PICO_SDK_PATH "$ENV{PICO_SDK_PATH}")
    else()
        message(FATAL_ERROR "PICO_SDK_PATH must identify a Pico SDK checkout")
    endif()
endif()

include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(karqri_pico C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

pico_sdk_init()
option(KARQRI_PICO_TRACE "Trace cooperative validation to a fixed RAM ring" OFF)

function(add_karqri_pico_firmware target main_source)
    add_executable(${target}
        ${main_source}
        pico_context.c
        pico_kernel.c
        pico_port.c
        pico_switch.S
        pico_tick.c
        pico_trace.c
        ../../kernel/event.c
        ../../kernel/queue.c
        ../../kernel/scheduler.c
        ../../kernel/scheduler_event.c
        ../../kernel/task.c
        ../../kernel/timer.c
        ../../kernel/timer_event.c
    )

    target_include_directories(${target} PRIVATE
        .
        ../../include
    )

    target_compile_options(${target} PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-Wall>
        $<$<COMPILE_LANGUAGE:C>:-Wextra>
        $<$<COMPILE_LANGUAGE:C>:-Werror>
    )

    target_link_libraries(${target}
        hardware_exception
        hardware_timer
        pico_stdlib
    )

    pico_enable_stdio_usb(${target} 1)
    pico_enable_stdio_uart(${target} 0)
    pico_add_extra_outputs(${target})
endfunction()

add_karqri_pico_firmware(
    karqri_pico_validation
    validation_main.c)
add_karqri_pico_firmware(
    karqri_pico_track_demo
    ../../demos/pico/track_demo.c)

target_compile_definitions(karqri_pico_validation PRIVATE
    KARQRI_PICO_TRACE=$<BOOL:${KARQRI_PICO_TRACE}>)
target_compile_definitions(karqri_pico_track_demo PRIVATE KARQRI_PICO_TRACE=0)

target_sources(karqri_pico_track_demo PRIVATE ../../demos/common/track_demo.c)
target_include_directories(karqri_pico_track_demo PRIVATE ../../demos/common)
