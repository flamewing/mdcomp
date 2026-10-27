# Decoder fixtures

`known_answers.cc` constructs compressed streams directly from descriptor and packet fields. It never calls a codec encoder or production bit/endian helpers. Expected output is assembled separately from literal bytes, repetitions, seeded history, and arithmetic sequences. The current set contains 94 fixtures covering all ten C++ decoders, including an explicit Saxman negative-origin match that must zero-fill the complete token.

`manifest.json` records every identifier, codec, source grammar, license, covered decoder forms, exact compressed/decompressed sizes, and SHA-256 hashes. Assembly-derived cases cite the shipped assembly source; LZKN1 and Nemesis cases explicitly cite decoder grammar. These are independent of the encoder under test, but reading a decoder or assembly grammar does not establish an algorithmically independent or executed assembly oracle. The master-generated Enigma vector in `codec/lzss_smoke.cc` remains a separately labelled shared-lineage reference case.

The descriptor assembler places early-refill words in wire order, including dummy words at exact boundaries. Continuous bit packets include zero-filled read-ahead guards. SnKRLE's odd-byte-count cases test the C++ decoder; they do not claim to satisfy the assembly routine's even-count precondition.

CTest decodes every fixture directly, from an embedded source offset with trailing bytes, and into a destination with an existing prefix. Exact output length/content is checked in all cases. Consumed input is asserted only for fixtures whose public API behavior is unambiguous; broad consumption/error contracts remain part of the buffer migration work.

The manifest also lists the initial required decoder forms for each codec. The integrity check exports the compiled fixtures below the build directory, verifies both hashes, and checks that those forms still have an annotated fixture. This covers the declared forms, not all legal parameter combinations, noncanonical encodings, or encoder-selection obligations. The separate `edges.json` manifest declares 50 encoder-form, 50 decoder-form, and 43 semantic-boundary obligations. `support/format_observer.cc` reads emitted bytes without production endian, bitstream, adaptor, or codec helpers, reconstructs the output, and records the forms actually encountered. Encoder recipes and hand-constructed decoder fixtures have separate coverage requirements. `encoder-edges.tsv` and `decoder-edges.tsv` retain one validated witness per observed form. `record_edge_audit.py --test-dir build-tests/tests --output tests/references/edge-audit.json` records reviewed coverage and source hashes. All declared obligations pass; this is not exhaustive coverage of every legal parameter, malformed stream, or target-specific interpretation. Supplemental fixtures now cover 8192-byte Kosinski/Plus distances, full-window Saxman/Rocket references, and Rocket one-byte matches.

To review a fixture change and update the manifest explicitly:

```sh
cmake --build build-tests --target mdcomp-fixture-export
build-tests/tests/mdcomp-fixture-export build-tests/tests/fixture-export
python tests/support/update_fixture_manifest.py --export-dir build-tests/tests/fixture-export --output tests/fixtures/manifest.json
ctest --test-dir build-tests -R fixture-manifest --output-on-failure
```

For Visual Studio, build with `--config Release`, use `build-tests/tests/Release/mdcomp-fixture-export.exe`, and pass `-C Release` to CTest. Export and hash verification run through CMake in the normal suite; Python is needed only for the explicit manifest update. Fixture bytes and their expected results must be reviewed before updating checksums to accept a change.

`external.json` and the generated `external.inc` contain 38 separately classified reference-generated encodings. Their synthetic raw inputs were checked against both self-decodes and both cross-decodes before capture. Original KENS fixtures come from the primary SourceForge RC3 distribution, not the differently built GitHub mirror. KENSSharp shares mdcomp ancestry. New fixtures cover Accurate Kosinski, Okumura-derived Saxman, ClownLZSS, and both ClownNemesis modes. Kirjavascript RLE fixtures convert only the tile-count header to mdcomp's byte count and preserve original/converted hashes. [External reference qualification](../references/external.md) records source pins, licenses, failures, limitations, and capture/check commands; [format-based verdicts](../references/format-verdicts.md) adjudicate disagreements. Saved vectors run offline in the ordinary C++ suite and do not imply that every output of those tools is correct.
