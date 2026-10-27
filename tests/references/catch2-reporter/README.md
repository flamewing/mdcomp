# Catch2 binary-vector reporter failure

During module interrupt testing, Catch2 3.8.1's console reporter crashed while rendering a failing comparison of two 64 KiB `std::vector<uint8_t>` values containing `0xa5` and `0xff`. This caused the Windows application-error dialog. A standalone reproducer confirms the reporter failure independently of the emulator. GDB stops in `Catch::TextFlow::Column::const_iterator::calcLength()`, reached through `operator++()`, `TextFlow::operator<<`, and `ConsoleReporter::assertionEnded()`. Depending on memory layout, the same reproduction can instead throw `basic_string::_M_create` while formatting the assertion. This is a reporter failure in the tested build, not a claim about every Catch2 version/platform.

The underlying assertion exposed a separate real codec defect: ComperX's interrupted module path saved D4 as a word although it holds a longword RLE pattern. The test now saves/restores D4 in full and checks VRAM with bounded first-mismatch diagnostics. Parenthesizing the vector equality prevents Catch2 from stringifying the whole byte range. Full actual/expected data is saved as binary artifacts on unexpected failure. The assembly-test main disables Windows critical-error/application-error dialogs so unattended failing tests do not require user interaction.

The standalone project deliberately contains a failing assertion and is not part of normal CTest runs. Run it only when investigating the reporter, preferably under a debugger:

```sh
cmake -S tests/references/catch2-reporter -B build-reporter -DCMAKE_PREFIX_PATH=/path/to/Catch2-install
cmake --build build-reporter
gdb --batch -ex run -ex "bt 20" --args build-reporter/reporter-repro
```

For Visual Studio, select the desired configuration and its executable subdirectory. A normal assertion-failure exit is expected even when the reporter does not crash. The reproduction was confirmed locally with GCC 16.2, Catch2 3.8.1, and Windows x64. Check current upstream versions before filing a separate report; no Catch2 issue has been submitted by this session.
