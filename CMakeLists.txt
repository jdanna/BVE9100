cmake_minimum_required(VERSION 3.16)
project(bve9100 C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

if(EMSCRIPTEN)
  # WebAssembly's own exception handling, for C++ exceptions and (below) Musashi's setjmp/longjmp
  add_compile_options(-fwasm-exceptions)
  add_link_options(-fwasm-exceptions)
endif()

# ---- Musashi 68k core (vendored) ----------------------------------------
set(MUSASHI ${CMAKE_SOURCE_DIR}/vendor/musashi)
set(MUSASHI_GEN ${CMAKE_BINARY_DIR}/musashi_gen)
file(MAKE_DIRECTORY ${MUSASHI_GEN})
add_executable(m68kmake ${MUSASHI}/m68kmake.c)
if(EMSCRIPTEN)
  # The generator runs at build time under node (CMAKE_CROSSCOMPILING_EMULATOR) and needs the real file system
  target_link_options(m68kmake PRIVATE -sNODERAWFS=1)
endif()
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
if(EMSCRIPTEN)
  # Musashi setjmps on every m68k_execute() for its bus-error trap: use WebAssembly's own exception handling for
  # it, as Emscripten's default JavaScript emulation would route every call in the CPU core through JavaScript
  target_compile_options(musashi PUBLIC -sSUPPORT_LONGJMP=wasm)
endif()

# ---- Emulator ------------------------------------------------------------
# Native: src/main.cpp (command line, HTTP front-end). WebAssembly (emcmake): src/web_main.cpp, the API the web
# version's worker calls (web/worker.js); same name, so the build makes bve9100.js + bve9100.wasm.
if(EMSCRIPTEN)
  set(BVE_MAIN src/web_main.cpp)
else()
  set(BVE_MAIN src/main.cpp)
endif()
add_executable(bve9100
  ${BVE_MAIN}
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
if(EMSCRIPTEN)
  target_link_options(bve9100 PRIVATE
    -sSUPPORT_LONGJMP=wasm -sMODULARIZE=1 -sEXPORT_NAME=createBve -sENVIRONMENT=worker
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=64MB -sFORCE_FILESYSTEM=1 -sEXIT_RUNTIME=0
    "-sEXPORTED_FUNCTIONS=['_bve_init','_bve_step','_bve_cycles','_bve_request','_bve_response_status','_bve_response_type','_bve_save','_malloc','_free']"
    "-sEXPORTED_RUNTIME_METHODS=['FS','ccall','cwrap','UTF8ToString']")
endif()

# ---- Tools ---------------------------------------------------------------
if(NOT EMSCRIPTEN)
  add_executable(m68kdis tools/m68kdis/m68kdis.cpp)
  target_link_libraries(m68kdis PRIVATE musashi)
endif()
