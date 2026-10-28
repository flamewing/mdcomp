#include "machine.hh"

#include <catch2/catch_session.hpp>

#include <iostream>

#ifdef _WIN32
#    define WIN32_LEAN_AND_MEAN
#    define NOMINMAX
#    include <windows.h>
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
#    include <crtdbg.h>
#endif

int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
#if defined(_MSC_VER) && defined(_DEBUG)
    // SetErrorMode does not suppress the Debug CRT's assertion dialogs.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
#endif
    std::cout << "Assembly dependencies: " << asm_test::dependency_versions() << '\n';
    return Catch::Session().run(argc, argv);
}
