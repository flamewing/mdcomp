cmake_minimum_required(VERSION 3.19)

project(mdcomp
    VERSION 0.2.0
    DESCRIPTION "Assorted compression formats for the Sega Mega Drive"
    LANGUAGES CXX ASM
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FORCE_COLORED_OUTPUT "Always produce ANSI-colored output (GNU/Clang only)." TRUE)

if(APPLE)
    set(UseLibCxxDefault TRUE)
else()
    set(UseLibCxxDefault FALSE)
endif()
option(UseLibCxx "Compile using libc++" UseLibCxxDefault)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(DEFINED ENV{MSYSTEM_PREFIX})
        set(CMAKE_INSTALL_PREFIX "$ENV{MSYSTEM_PREFIX}" CACHE PATH "Default install prefix" FORCE)
    endif(DEFINED ENV{MSYSTEM_PREFIX})
endif(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
include(GNUInstallDirs)

find_package(Boost 1.54 REQUIRED)

include(CheckCXXCompilerFlag)

function(add_compile_options_safe FLAG)
    string(REGEX REPLACE "[-=+]" "" FLAG_NO_SIGNS ${FLAG}) # <- The variable recieving the result of the test can't have those signs in its name
    check_cxx_compiler_flag(${FLAG} CXX_COMPILER_SUPPORTS_${FLAG_NO_SIGNS})
    if(CXX_COMPILER_SUPPORTS_${FLAG_NO_SIGNS})
        add_compile_options("${FLAG}")
    endif(CXX_COMPILER_SUPPORTS_${FLAG_NO_SIGNS})
endfunction()

if(UseLibCxx AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "You can only use libc++ with clang" )
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    if (${FORCE_COLORED_OUTPUT})
        if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-fcolor-diagnostics -fansi-escape-codes)
            add_link_options(-fcolor-diagnostics -fansi-escape-codes)
        elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            add_compile_options(-fdiagnostics-color=always)
            add_link_options(-fdiagnostics-color=always)
        endif()
    endif()
    if (WARNINGS_ARE_ERRORS)
        add_compile_options(-Werror)
        add_compile_options_safe(-pedantic-errors)
    endif()
    if(CMAKE_BUILD_TYPE MATCHES "Debug")
        add_compile_options_safe(-Og)
        add_compile_options_safe(-g3)
    endif()
    if(UseLibCxx)
        add_compile_options(-stdlib=libc++)
        add_link_options(-stdlib=libc++ -lc++)
        # add_link_options("-lc++abi")
    endif()
    add_compile_options(-Wall)
    add_compile_options(-Wextra)
    add_compile_options(-pedantic)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-Wno-c++14-compat)
        add_compile_options(-Wno-c++17-compat)
        add_compile_options_safe(-Wno-c++20-compat)
        add_compile_options_safe(-Wno-c++14-compat-pedantic)
        add_compile_options_safe(-Wno-c++17-compat-pedantic)
        add_compile_options_safe(-Wno-c++20-compat-pedantic)
        add_compile_options_safe(-Wno-pre-c++14-compat)
        add_compile_options_safe(-Wno-pre-c++17-compat)
        add_compile_options_safe(-Wno-pre-c++20-compat)
        add_compile_options_safe(-Wno-pre-c++14-compat-pedantic)
        add_compile_options_safe(-Wno-pre-c++17-compat-pedantic)
        add_compile_options_safe(-Wno-pre-c++20-compat-pedantic)
    endif()
    add_compile_options_safe(-Walloc-zero)
    add_compile_options(-Walloca)
    add_compile_options_safe(-Wanalyzer-too-complex)
    add_compile_options_safe(-Warith-conversion)
    add_compile_options_safe(-Warray-bounds-pointer-arithmetic)
    add_compile_options_safe(-Wassign-enum)
    add_compile_options_safe(-Wbad-function-cast)
    add_compile_options_safe(-Wbitwise-op-parentheses)
    add_compile_options_safe(-Wbraced-scalar-init)
    add_compile_options_safe(-Wbridge-cast)
    add_compile_options(-Wcast-align)
    add_compile_options_safe(-Wcast-align=strict)
    add_compile_options(-Wcast-qual)
    add_compile_options_safe(-Wchar-subscripts)
    add_compile_options_safe(-Wcomma)
    add_compile_options_safe(-Wcomma-subscript)
    add_compile_options_safe(-Wcomment)
    add_compile_options_safe(-Wconditionally-supported)
    add_compile_options_safe(-Wcovered-switch-default)
    add_compile_options(-Wctor-dtor-privacy)
    add_compile_options(-Wdate-time)
    add_compile_options_safe(-Wdeprecated-copy)
    add_compile_options_safe(-Wdeprecated-copy-dtor)
    add_compile_options_safe(-Wdeprecated-dynamic-exception-spec)
    add_compile_options(-Wdisabled-optimization)
    add_compile_options(-Wdouble-promotion)
    add_compile_options_safe(-Wduplicated-branches)
    add_compile_options_safe(-Wduplicated-cond)
    add_compile_options_safe(-Wempty-init-stmt)
    add_compile_options(-Wextra-semi)
    add_compile_options_safe(-Wfor-loop-analysis)
    add_compile_options(-Wformat-nonliteral)
    add_compile_options(-Wformat-security)
    add_compile_options_safe(-Wformat-signedness)
    add_compile_options(-Wformat-y2k)
    add_compile_options_safe(-Wfour-char-constants)
    add_compile_options_safe(-Wgcc-compat)
    add_compile_options_safe(-Wheader-hygiene)
    add_compile_options_safe(-Widiomatic-parentheses)
    add_compile_options_safe(-Wint-in-bool-context)
    add_compile_options(-Winvalid-pch)
    add_compile_options_safe(-Wlogical-op)
    add_compile_options_safe(-Wlogical-op-parentheses)
    add_compile_options_safe(-Wmany-braces-around-scalar-init)
    add_compile_options_safe(-Wmisleading-indentation)
    add_compile_options_safe(-Wmismatched-tags)
    # add_compile_options(-Wmissing-braces)
    add_compile_options(-Wmissing-include-dirs)
    add_compile_options_safe(-Wmove)
    add_compile_options(-Wmultichar)
    add_compile_options_safe(-Wnoexcept)
    add_compile_options(-Wnon-virtual-dtor)
    add_compile_options(-Wnull-dereference)
    add_compile_options(-Wold-style-cast)
    add_compile_options_safe(-Wover-aligned)
    add_compile_options(-Woverloaded-virtual)
    add_compile_options(-Wpacked)
    add_compile_options(-Wredundant-decls)
    add_compile_options_safe(-Wredundant-tags)
    add_compile_options(-Wregister)
    add_compile_options_safe(-Wshadow=compatible-local)
    add_compile_options_safe(-Wshadow=local)
    add_compile_options_safe(-Wshadow-field)
    add_compile_options_safe(-Wshadow-field-in-constructor)
    add_compile_options_safe(-Wshadow-field-in-constructor-modified)
    add_compile_options_safe(-Wshadow-ivar)
    add_compile_options_safe(-Wshadow-uncaptured-local)
    # add_compile_options(-Wsign-promo)
    add_compile_options(-Wstack-protector)
    add_compile_options_safe(-Wstrict-null-sentinel)
    add_compile_options_safe(-Wstring-conversion)
    add_compile_options_safe(-Wstring-plus-char)
    add_compile_options_safe(-Wsuggest-attribute=cold)
    add_compile_options_safe(-Wsuggest-attribute=const)
    add_compile_options_safe(-Wsuggest-attribute=format)
    add_compile_options_safe(-Wsuggest-attribute=malloc)
    add_compile_options_safe(-Wsuggest-attribute=noreturn)
    add_compile_options_safe(-Wsuggest-attribute=pure)
    add_compile_options_safe(-Wsuggest-final-methods)
    add_compile_options_safe(-Wsuggest-final-types)
    add_compile_options_safe(-Wsuggest-destructor-override)
    add_compile_options_safe(-Wsuggest-override)
    # add_compile_options(-Wswitch-default)
    add_compile_options(-Wswitch-enum)
    add_compile_options(-Wsynth)
    add_compile_options_safe(-Wtrampolines)
    add_compile_options(-Wundef)
    add_compile_options_safe(-Wunused-label)
    add_compile_options_safe(-Wunused-lambda-capture)
    add_compile_options_safe(-Wunused-local-typedef)
    add_compile_options(-Wunused-macros)
    add_compile_options_safe(-Wuseless-cast)
    add_compile_options_safe(-Wvector-conversion)
    add_compile_options_safe(-Wvector-operation-performance)
    add_compile_options_safe(-Wvirtual-inheritance)
    add_compile_options_safe(-Wvolatile)
    add_compile_options(-Wzero-as-null-pointer-constant)
    add_compile_options_safe(-Warray-bounds=2)
    add_compile_options_safe(-Wattribute-alias=2)
    add_compile_options_safe(-Wcatch-value=3)
    add_compile_options_safe(-Wformat-overflow=2)
    add_compile_options_safe(-Wformat-truncation=2)
    add_compile_options_safe(-Wformat=2)
    add_compile_options(-Wimplicit-fallthrough)
    add_compile_options_safe(-Wimplicit-fallthrough=3)
    add_compile_options_safe(-Wplacement-new=2)
    add_compile_options_safe(-Wshift-overflow=2)
    add_compile_options_safe(-Wstrict-aliasing=3)
    add_compile_options(-Wstrict-overflow=2)
    add_compile_options_safe(-Wstringop-overflow=4)
    add_compile_options(-Wunused-const-variable)
    add_compile_options_safe(-Wunused-const-variable=1)
elseif (MSVC)
    if (WARNINGS_ARE_ERRORS)
        add_compile_options("/WX")
    endif()
    add_compile_options("/W4")
    add_compile_options("/wd4018") # warning C4018: '>': signed/unsigned mismatch
    add_compile_options("/wd4127") # warning C4127: conditional expression is constant
    add_compile_options("/wd4244") # warning C4244: 'initializing': conversion from 'int' to 'char', possible loss of data
    add_compile_options("/wd4251")
    # Clang: -Wshorten-64-to-32 -Wimplicit-int-conversion
    add_compile_options("/wd4267") # warning C4267: 'return': conversion from 'size_t' to 'int', possible loss of data
    add_compile_options("/wd4389") # warning C4389: '==': signed/unsigned mismatch
    add_compile_options("/wd4482")
    add_compile_options("/wd4512")
    add_compile_options("/wd4701") # warning C4701: potentially uninitialized local variable 'err' used
    add_compile_options("/wd4706") # warning C4706: assignment within conditional expression
    add_compile_options("/wd4800") # warning C4800: 'const SymbolDatabase *' : forcing value to bool 'true' or 'false' (performance warning)
endif()

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    # If Microsoft SDK is installed create script run-msbuild.bat that
    # calls SetEnv.cmd to set up build environment and runs msbuild.
    # It is useful when building Visual Studio projects with the SDK
    # toolchain rather than Visual Studio.
    include(FindSetEnv)
    if (WINSDK_SETENV)
        set(MSBUILD_SETUP "call \"${WINSDK_SETENV}\"")
    endif ()
    # Set FrameworkPathOverride to get rid of MSB3644 warnings.
    set(netfxpath "C:\\Program Files\\Reference Assemblies\\Microsoft\\Framework\\.NETFramework\\v4.0")
    file(WRITE run-msbuild.bat "
    ${MSBUILD_SETUP}
    ${CMAKE_MAKE_PROGRAM} -p:FrameworkPathOverride=\"${netfxpath}\" %*")
endif()

set(COMMON_HEADERS
    "include/mdcomp/basic_decoder.hh"
    "include/mdcomp/bigendian_io.hh"
    "include/mdcomp/bitstream.hh"
    "include/mdcomp/ignore_unused_variable_warning.hh"
    "include/mdcomp/lzss.hh"
    "include/mdcomp/moduled_adaptor.hh"
)

# Interface library for a few exported headers.
set(BIGENDIANIO_HEADERS
    "include/mdcomp/bigendian_io.hh"
    "include/mdcomp/bitstream.hh"
    "include/mdcomp/ignore_unused_variable_warning.hh"
)
add_library(bigendian_io INTERFACE "${BIGENDIANIO_HEADERS}")
add_library(mdcomp::bigendian_io ALIAS bigendian_io)
target_include_directories(bigendian_io
    INTERFACE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(bigendian_io
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE ON
        PUBLIC_HEADER "${BIGENDIANIO_HEADERS}"
)

# Dummy library for generating compile_commands.json that
# sets flags for headers without corresponding cc files.
add_library(dummy-mdcomp
    "src/lib/basic_decoder.cc"
    "src/lib/bigendian_io.cc"
    "src/lib/bitstream.cc"
    "src/lib/ignore_unused_variable_warning.cc"
    "src/lib/lzss.cc"
    "src/lib/moduled_adaptor.cc"
    "${COMMON_HEADERS}"
)
target_include_directories(dummy-mdcomp
    PRIVATE
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
set_target_properties(dummy-mdcomp
    PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
        POSITION_INDEPENDENT_CODE ON
)

function(define_lib_internal TYPE TARGETNAME OUTPUT SRCFILE HDRFILE)
    if("${TYPE}" STREQUAL "STATIC")
        add_library(${TARGETNAME}
            STATIC
                "${SRCFILE}"
                "${HDRFILE}"
                "${COMMON_HEADERS}"
        )
        set_target_properties(${TARGETNAME} PROPERTIES OUTPUT_NAME ${OUTPUT})
    else()
        add_library(${TARGETNAME}
            SHARED
                "${SRCFILE}"
                "${HDRFILE}"
                "${COMMON_HEADERS}"
        )
    endif()
    add_library(mdcomp::${TARGETNAME} ALIAS ${TARGETNAME})
    target_include_directories(${TARGETNAME}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${TARGETNAME}
        INTERFACE
            bigendian_io
    )
    set_target_properties(${TARGETNAME}
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
            POSITION_INDEPENDENT_CODE ON
            PUBLIC_HEADER "${HDRFILE};${COMMON_HEADERS}"
    )
endfunction()

function(define_lib TARGETNAME SRCFILE HDRFILE)
    define_lib_internal(
        "STATIC"
        "${TARGETNAME}Static"
        "${TARGETNAME}"
        "${SRCFILE}"
        "${HDRFILE}"
        "${COMMON_HEADERS}"
    )
    define_lib_internal(
        "SHARED"
        "${TARGETNAME}"
        "${TARGETNAME}"
        "${SRCFILE}"
        "${HDRFILE}"
        "${COMMON_HEADERS}"
    )
endfunction()

function(define_exe TARGETNAME SRCFILE LIBNAME EXENAME)
    add_executable(${TARGETNAME}
        "${SRCFILE}"
    )
    target_include_directories(${TARGETNAME}
        PUBLIC
            $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )
    target_link_libraries(${TARGETNAME} PUBLIC ${LIBNAME})
    set_target_properties(${TARGETNAME}
        PROPERTIES
            CXX_STANDARD 20
            CXX_STANDARD_REQUIRED YES
            CXX_EXTENSIONS NO
            POSITION_INDEPENDENT_CODE ON
            OUTPUT_NAME ${EXENAME}
    )
endfunction()

define_lib(artc42   "src/lib/artc42.cc"   "include/mdcomp/artc42.hh")
define_lib(comper   "src/lib/comper.cc"   "include/mdcomp/comper.hh")
define_lib(comperx  "src/lib/comperx.cc"  "include/mdcomp/comperx.hh")
define_lib(enigma   "src/lib/enigma.cc"   "include/mdcomp/enigma.hh")
define_lib(kosinski "src/lib/kosinski.cc" "include/mdcomp/kosinski.hh")
define_lib(kosplus  "src/lib/kosplus.cc"  "include/mdcomp/kosplus.hh")
define_lib(lzkn1    "src/lib/lzkn1.cc"    "include/mdcomp/lzkn1.hh")
define_lib(nemesis  "src/lib/nemesis.cc"  "include/mdcomp/nemesis.hh")
define_lib(rocket   "src/lib/rocket.cc"   "include/mdcomp/rocket.hh")
define_lib(saxman   "src/lib/saxman.cc"   "include/mdcomp/saxman.hh")
define_lib(snkrle   "src/lib/snkrle.cc"   "include/mdcomp/snkrle.hh")

define_exe(compercmp   "src/tools/compcmp.cc"  comper   compcmp)
define_exe(comperxcmp  "src/tools/comperx.cc"  comperx  comperx)
define_exe(enigmacmp   "src/tools/enicmp.cc"   enigma   enicmp)
define_exe(kosinskicmp "src/tools/koscmp.cc"   kosinski koscmp)
define_exe(kospluscmp  "src/tools/kosplus.cc"  kosplus  kosplus)
define_exe(lzkn1cmp    "src/tools/lzkn1cmp.cc" lzkn1    lzkn1cmp)
define_exe(nemesiscmp  "src/tools/nemcmp.cc"   nemesis  nemcmp)
define_exe(rocketcmp   "src/tools/rockcmp.cc"  rocket   rockcmp)
define_exe(saxmancmp   "src/tools/saxcmp.cc"   saxman   saxcmp)
define_exe(snkrlecmp   "src/tools/snkcmp.cc"   snkrle   snkcmp)

file(GLOB_RECURSE ALL_SOURCE_FILES *.cc *.hh)

add_custom_target(
    mdcomp-format
    COMMAND clang-format
    -style=file
    -i
    ${ALL_SOURCE_FILES}
)

add_custom_target(
    mdcomp-tidy
    COMMAND clang-tidy --use-color
    ${ALL_SOURCE_FILES}
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    mdcompConfigVersion.cmake
    VERSION ${PACKAGE_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(
    TARGETS
        bigendian_io
        artc42
        artc42Static
        comper
        comperStatic
        comperx
        comperxStatic
        enigma
        enigmaStatic
        kosinski
        kosinskiStatic
        kosplus
        kosplusStatic
        lzkn1
        lzkn1Static
        nemesis
        nemesisStatic
        rocket
        rocketStatic
        saxman
        saxmanStatic
        snkrle
        snkrleStatic
        compercmp
        comperxcmp
        enigmacmp
        kosinskicmp
        kospluscmp
        lzkn1cmp
        nemesiscmp
        rocketcmp
        saxmancmp
        snkrlecmp
    EXPORT
        mdcompConfig
    LIBRARY
        COMPONENT Libraries
        DESTINATION lib
        NAMELINK_COMPONENT Development
    RUNTIME
        COMPONENT Tools
        DESTINATION bin
    PUBLIC_HEADER
        COMPONENT Development
        DESTINATION include/mdcomp
)

export(
    TARGETS
        bigendian_io
        artc42
        artc42Static
        comper
        comperStatic
        comperx
        comperxStatic
        enigma
        enigmaStatic
        kosinski
        kosinskiStatic
        kosplus
        kosplusStatic
        lzkn1
        lzkn1Static
        nemesis
        nemesisStatic
        rocket
        rocketStatic
        saxman
        saxmanStatic
        snkrle
        snkrleStatic
    NAMESPACE
        mdcomp::
    FILE
        mdcompConfig.cmake
)

set(CMAKE_EXPORT_PACKAGE_REGISTRY ON)
export(PACKAGE mdcomp)
