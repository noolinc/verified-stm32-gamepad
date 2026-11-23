cmake_minimum_required(VERSION 3.20)
project(nool_gamepad C)

# C edition must match policy/hardware-profile.toml [language] (EMB-C-022).
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_library(gamepad_core STATIC
  core/gp_input.c
  core/gp_report.c
  core/usb_ctrl.c
  core/usb_desc.c
)
target_include_directories(gamepad_core PUBLIC core)
target_compile_options(gamepad_core PRIVATE -Wall -Wextra -Werror -Wconversion -Wsign-conversion)

if(CMAKE_CROSSCOMPILING)
  add_subdirectory(firmware/stm32f411)
else()
  enable_testing()
  add_subdirectory(tests)
endif()
