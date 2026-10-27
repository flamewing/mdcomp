# Keep upstream C cores isolated from mdcomp's warning policy.
set_property(DIRECTORY PROPERTY COMPILE_OPTIONS "")
set(musashi "${MDCOMP_MUSASHI_SOURCE_DIR}")
set(generated "${CMAKE_CURRENT_BINARY_DIR}/musashi-generated")
file(MAKE_DIRECTORY "${generated}")
add_executable(mdcomp-m68kmake "${musashi}/m68kmake.c")
if(MSVC)
    # MSVC 19.44 /O2 produces incorrect opcode masks in this generator.
    # This affects only the build-time tool, not the emulator core.
    target_compile_options(mdcomp-m68kmake PRIVATE /Od)
endif()
add_custom_command(OUTPUT "${generated}/m68kops.c" "${generated}/m68kops.h"
    COMMAND mdcomp-m68kmake "${generated}" "${musashi}/m68k_in.c"
    DEPENDS mdcomp-m68kmake "${musashi}/m68k_in.c" VERBATIM)
add_library(mdcomp-musashi STATIC "${musashi}/m68kcpu.c" "${generated}/m68kops.c" "${musashi}/softfloat/softfloat.c")
target_include_directories(mdcomp-musashi PUBLIC "${musashi}" PRIVATE "${generated}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_definitions(mdcomp-musashi PRIVATE MUSASHI_CNF="mdcomp_m68kconf.h")
add_library(mdcomp-z80 STATIC "${MDCOMP_Z80_SOURCE_DIR}/z80.c")
target_include_directories(mdcomp-z80 PUBLIC "${MDCOMP_Z80_SOURCE_DIR}")
foreach(target mdcomp-m68kmake mdcomp-musashi mdcomp-z80)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
    if(MSVC)
        target_compile_options(${target} PRIVATE /w)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endforeach()
