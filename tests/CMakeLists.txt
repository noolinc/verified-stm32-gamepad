# Host tests: unit tests (test_*) and property-based tests (pbt_*).
set(UNIT_TESTS test_report test_input test_descriptor test_usb_ctrl)
set(PBT_TESTS pbt_report pbt_input pbt_usb_ctrl)

foreach(t IN LISTS UNIT_TESTS PBT_TESTS)
  add_executable(${t} ${t}.c usb_fake.c)
  target_link_libraries(${t} PRIVATE gamepad_core)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(${t} PRIVATE -Wall -Wextra -Werror)
  add_test(NAME ${t} COMMAND ${t})
endforeach()

# Pin the PBT seed in CI-style runs unless the caller overrides it.
set_tests_properties(${PBT_TESTS} PROPERTIES ENVIRONMENT "PBT_RUNS=2000")
