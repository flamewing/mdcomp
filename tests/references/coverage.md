# Diagnostic compiler coverage

Select the `coverage` input when manually running `ci-linux` to add a GCC 13 line/branch coverage job. It runs the C++ baseline, installed consumers, and deterministic stress sweep, then uploads Cobertura XML. Assembly execution belongs to the separate normal/sanitizer jobs. A report must contain nonzero measured and covered lines/branches; there is no percentage threshold. Compiler instrumentation supplements the required format-edge obligations and cannot establish correctness or compile-time coverage.

For a local Linux run, use an installed Catch2 v3 package and Boost headers. Start with a fresh build directory so counters from old binaries cannot be mixed into the report:

```sh
python3 -m venv build-coverage/venv
build-coverage/venv/bin/pip install -r tests/support/coverage-requirements.txt
cmake -S . -B build-coverage -G Ninja -DBUILD_TESTING=ON -DMDCOMP_ASSEMBLY_TESTS=OFF -DMDCOMP_STRESS_TESTS=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_FLAGS=--coverage -DCMAKE_CXX_FLAGS=--coverage
cmake --build build-coverage --parallel
ctest --test-dir build-coverage --output-on-failure --no-tests=error
build-coverage/venv/bin/gcovr --gcov-executable gcov --root . --filter 'src/lib/' --filter 'include/mdcomp/' --xml-pretty --output build-coverage/coverage.xml build-coverage
```

Use `gcov` from the same GCC release used to compile the project. The isolated reporter requirements pin gcovr 8.7 and its dependencies; normal CTest runs do not require Python. Local GCC 15.2 qualification passed 126 CTest cases and produced a nonempty report (4,751/5,960 lines and 3,284/5,598 branches). These are diagnostic results for that compiler and corpus, not portable coverage targets. Remote CI has not yet been observed.
