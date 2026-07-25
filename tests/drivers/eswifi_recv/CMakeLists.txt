# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(eswifi_recv_fault_injection_test)

# Which driver implementation to link: "fixed" (default) or "buggy".
# A plain CMake cache variable, not a Kconfig option -- this only picks
# a source file, it isn't a build configuration the module itself needs
# to know about, so it doesn't belong in Kconfig.
set(ESWIFI_RECV_VARIANT "fixed" CACHE STRING "eswifi_recv test variant: fixed or buggy")

target_sources(app PRIVATE src/test_eswifi_recv_ztest.c)

if(ESWIFI_RECV_VARIANT STREQUAL "buggy")
  target_sources(app PRIVATE src/eswifi_repro_buggy.c)
elseif(ESWIFI_RECV_VARIANT STREQUAL "fixed")
  target_sources(app PRIVATE src/eswifi_repro_fixed.c)
else()
  message(FATAL_ERROR "ESWIFI_RECV_VARIANT must be 'fixed' or 'buggy', got '${ESWIFI_RECV_VARIANT}'")
endif()

target_include_directories(app PRIVATE src)
