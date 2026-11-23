# STM32F411 firmware image (cross build: cmake --preset firmware).
add_executable(gamepad
  main.c
  startup.c
  clock.c
  board.c
  usb_otg_fs.c
  mem.c
)
set_target_properties(gamepad PROPERTIES SUFFIX ".elf")
target_include_directories(gamepad PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(gamepad PRIVATE gamepad_core gcc)
target_compile_options(gamepad PRIVATE -Wall -Wextra -Werror -Wconversion)
target_link_options(gamepad PRIVATE
  -T${CMAKE_CURRENT_SOURCE_DIR}/stm32f411.ld
  -nostdlib
  -Wl,--gc-sections -Wl,-Map=$<TARGET_FILE_DIR:gamepad>/gamepad.map -Wl,--print-memory-usage)
add_custom_command(TARGET gamepad POST_BUILD
  COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:gamepad> $<TARGET_FILE_DIR:gamepad>/gamepad.bin
  COMMAND ${CMAKE_SIZE} $<TARGET_FILE:gamepad>
  COMMENT "gamepad.bin")
