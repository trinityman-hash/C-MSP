if(CONFIG_FAULT_INJECTION)

  # Real enforcement of "must not ship in a non-test build": a Kconfig
  # bool default cannot fail a build by itself, so this is a CMake-time
  # hard error instead. CONFIG_ZTEST is what Zephyr sets for any Ztest
  # test image, which is the only intended consumer of this module today.
  if(NOT CONFIG_ZTEST)
    message(FATAL_ERROR
      "CONFIG_FAULT_INJECTION=y outside a Ztest build. This module's "
      "design requires fault injection to be physically absent from "
      "non-test images -- see docs/brief.md and Kconfig for why. If "
      "you have a legitimate new consumer, that's a deliberate decision "
      "to make, not something to bypass here.")
  endif()

  zephyr_include_directories(include)
  zephyr_library()
  zephyr_library_sources(src/fault_inject.c)

endif()
