# Running tests

Configure with CMake 3.30 or newer and an installed Catch2 v3 package. Catch2 is a test-only dependency; use `-DBUILD_TESTING=OFF` for a library/tool build without it. Tests are never installed or exported with mdcomp.

```sh
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests --parallel
ctest --test-dir build-tests --output-on-failure
ctest --test-dir build-tests -L unit --output-on-failure
```

For Visual Studio, add `--config Release` to the build and `-C Release` to CTest. To run a single case, use CTest's `-R` filter. Each unit case has a 30-second timeout and runs below the build directory. Constant-evaluated checks compile as part of the same unit target; runtime cases exercise the runtime implementations separately.

With vcpkg, select the manifest's test feature during configuration:

```sh
cmake -S . -B build-tests -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_MANIFEST_FEATURES=tests -DBUILD_TESTING=ON
```

Other dependency managers can supply Catch2 through `CMAKE_PREFIX_PATH` or `Catch2_DIR`. For a reproducible standalone installation of the version used to bootstrap this suite:

```sh
git clone --branch v3.8.1 --depth 1 https://github.com/catchorg/Catch2.git build-deps/Catch2
cmake -S build-deps/Catch2 -B build-deps/catch-build -DCATCH_BUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX="$PWD/build-deps/catch-install"
cmake --build build-deps/catch-build --config Release --parallel
cmake --install build-deps/catch-build --config Release
cmake -S . -B build-tests -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$PWD/build-deps/catch-install"
```

The initial suite covers endian I/O. Codec adapters, independent fixtures, and assembly harnesses remain tracked in [the testing plan](../docs/testing-framework-plan.md); passing these unit tests does not complete the pre-refactor baseline gate.

Endian stream operations preserve ordinary `istream`/`ostream` state and exception-mask behavior. Direct `streambuf` transfers throw `std::ios_base::failure` when `sgetn` or `sputn` transfers fewer bytes than requested; an underlying exception propagates. A failed transfer can consume or produce a prefix and does not roll back. Endian input iterators yield complete values only, read their first value during construction, and become end iterators on an incomplete read when the stream does not throw.

`MDCOMP_HAS_INT128` indicates availability of `utils::int128_t`, `utils::uint128_t`, and the `read16`/`write16` entry points. Typed I/O, `read_n<16>`/`write_n<16>`, and byte swapping also accept these types. GCC and Clang use native 128-bit integers, including in strict C++20 mode. Microsoft STL builds without native 128-bit integers use its internal `std::_Signed128` and `std::_Unsigned128` classes when `<__msvc_int128.hpp>` is available; these are implementation-specific types, so that path is checked by the MSVC CI job. Other integer types retain the standard integral concept checks.
