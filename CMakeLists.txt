cmake_minimum_required(VERSION 3.16)
project(bve9100 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# ---- Musashi 68k core (vendored) ----------------------------------------
set(MUSASHI ${CMAKE_SOURCE_DIR}/vendor/musashi)
set(MUSASHI_GEN ${CMAKE_BINARY_DIR}/musashi_gen)
file(MAKE_DIRECTORY ${MUSASHI_GEN})
add_executable(m68kmake ${MUSASHI}/m68kmake.c)
add_custom_command(
  OUTPUT ${MUSASHI_GEN}/m68kops.c ${MUSASHI_GEN}/m68kops.h
  COMMAND m68kmake ${MUSASHI_GEN} ${MUSASHI}/m68k_in.c
  DEPENDS m68kmake ${MUSASHI}/m68k_in.c
  COMMENT "Generating Musashi opcode tables")
add_library(musashi STATIC
  ${MUSASHI}/m68kcpu.c
  ${MUSASHI}/m68kdasm.c
  ${MUSASHI}/softfloat/softfloat.c
  ${MUSASHI_GEN}/m68kops.c)
# Our m68kconf.h must be found before Musashi's own copy.
target_include_directories(musashi PUBLIC ${CMAKE_SOURCE_DIR}/src/cpu ${MUSASHI_GEN} ${MUSASHI})
target_compile_options(musashi PRIVATE -w)

# ---- Emulator ------------------------------------------------------------
add_executable(bve9100
  src/main.cpp
  src/bus.cpp
  src/machine.cpp
  src/loader.cpp
  src/tty_frontend.cpp
  src/httpd.cpp
  src/cpu/cpu_glue.cpp
  src/devices/pit8254.cpp
  src/devices/pic8259.cpp
  src/devices/usart8251.cpp
  src/devices/dmac.cpp
  src/devices/mailbox.cpp
  src/devices/if508.cpp
  src/devices/rs422.cpp
  src/devices/idc_peers.cpp
  src/devices/rtc.cpp
  src/devices/fdc.cpp
  src/devices/scsi_wd33c93.cpp
  src/devices/cpu88_misc.cpp
  src/devices/dsc63.cpp
  src/devices/spf.cpp
  src/devices/eeprom.cpp)
target_include_directories(bve9100 PRIVATE src)
target_link_libraries(bve9100 PRIVATE musashi)
target_compile_options(bve9100 PRIVATE -Wall -Wextra -Wno-unused-parameter)

# ---- Tools ---------------------------------------------------------------
add_executable(m68kdis tools/m68kdis/m68kdis.cpp)
target_link_libraries(m68kdis PRIVATE musashi)
