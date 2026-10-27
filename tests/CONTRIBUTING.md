# Adding codec tests

Start with the format’s wire rules and a failing test. Keep encoder choice, decoder acceptance, and semantic boundaries as separate obligations in [fixtures/edges.json](fixtures/edges.json). New variants need their own obligations and qualifications. Line coverage helps locate untested code, but does not establish format correctness.

1. Identify the normative format description and exact variant. Record any ambiguity, target-specific behavior, source lineage, or redistribution restriction before choosing expected bytes.
2. Construct compressed vectors without calling the encoder under test. Use [wire_builder.hh](support/wire_builder.hh) for descriptor fields or explicit byte/packet construction. Build expected output separately from literals, arithmetic sequences, or initialized history.
3. Add the vectors to [known_answers.cc](fixtures/known_answers.cc), with a stable identifier and form annotations, and demonstrate the decoder failure before fixing it. Include the smallest reproduction of every fixed bug.
4. Add the codec to the common [API adapter](support/codec_api.hh), its implementation and CMake link list, and the applicable basic/moduled template cases. Run the same contract tests through each supported interface; new buffer interfaces must retain the stream fixtures during migration.
5. Add a bounded wire inspector independent of production endian, bitstream, adaptor, and codec helpers. Validate it against hand-authored expected output before using it to classify encoder output.
6. Add deterministic source recipes that cause the encoder to emit its required forms. Require independently reconstructed bytes to match the padded input, and require the observed form rather than assuming that round trips selected it. Preserve seed, case identifier, input, and encoding on failure.
7. Use qualified external encodings and applicable assembly decoders for additional checks. Bootstrap assembly variants from non-current-encoder data first. Keep shared authorship, original-derived imports, unknown deployment, emulator verification, and real-hardware evidence separate.
8. Review fixture changes before explicitly updating the hash manifest and audit summaries. Do not regenerate expected bytes from the implementation being fixed. For shortest-output encoders, different valid bytes are acceptable; an accurate-encoding claim needs its own exact-byte contract.

A fixture descriptor has this shape; replace the placeholders with independently constructed bytes and a separately derived expected result:

```cpp
codec_test::fixture example{
    "codec/form-boundary", "codec", compressed, expected,
    {"form", "boundary"}, settings, expected_consumed
};
```

The common adapter describes codec name, input granularity, output alignment, basic encode/decode callbacks, and optional module callbacks. [codec_api.cc](support/codec_api.cc) contains copyable wrappers for plain codecs, Saxman’s explicit framing option, and configurable module padding. This adapter is test infrastructure and does not add public library ABI.

Use malformed/truncated cases where the current API has a defined failure contract. The checked-memory migration must additionally verify exact-capacity success, failure before output mutation, canaries, alignment diagnostics, and prepare/commit behavior. Those assertions depend on the future buffer interfaces; a permissive stream decoder must not be advertised as a checked parser.

# Running a focused check

```sh
ctest --test-dir build-tests -R 'Hand-constructed.*kosinski' --output-on-failure --no-tests=error
ctest --test-dir build-tests -L edges --output-on-failure --no-tests=error
build-tests/tests/mdcomp-codec-smoke-tests 'Hand-constructed codec streams decode to independent expected bytes - kosinski' --section 'kosinski/match-0-2'
build-tests/tests/mdcomp-fixture-export build-tests/tests/fixture-export
build-tests/tests/asm/mdcomp-asm-run kosinski build-tests/tests/fixture-export/kosinski/match-0-2.compressed build-tests/one-68000.bin --output-size 3
build-tests/tests/asm/mdcomp-asm-run kosinski-z80 build-tests/tests/fixture-export/kosinski/match-0-2.compressed build-tests/one-z80.bin --output-size 3
```

The two runner commands execute the same fixture on one architecture at a time. See [assembly qualification and timing](asm/README.md) for instruction/cycle limits, source alignment, read-ahead, and core identities. With Visual Studio, add `-C Release` to CTest and use the executables’ `Release` subdirectory. Native Windows executables also have the `.exe` suffix.

Enable the optional deterministic sweep with `-DMDCOMP_STRESS_TESTS=ON`, build, then run `ctest --test-dir build-tests -L stress --output-on-failure --no-tests=error`. Its four fixed seeds and 64 cases per codec produce 2,560 basic encoder/decoder/observer comparisons, with source sizes bounded at 2,048 bytes and a separate 600-second timeout. Scheduled and manual Linux CI enables this sweep in the Clang/Ninja sanitizer job. Normal push/PR jobs retain the ordinary suite.
