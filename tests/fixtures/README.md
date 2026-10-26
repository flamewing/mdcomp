# Hand-constructed decoder fixtures

`known_answers.cc` constructs compressed streams directly from descriptor and packet fields. It never calls a codec encoder or production bit/endian helpers. Expected output is assembled separately from literal bytes, repetitions, seeded history, and arithmetic sequences. The current set contains 86 fixtures covering all ten C++ decoders.

`manifest.json` records every identifier, codec, source grammar, license, covered decoder forms, exact compressed/decompressed sizes, and SHA-256 hashes. Assembly-derived cases cite the shipped assembly source; LZKN1 and Nemesis cases explicitly cite decoder grammar. These are independent of the encoder under test, but reading a decoder or assembly grammar does not establish an algorithmically independent or executed assembly oracle. The master-generated Enigma vector in `codec/lzss_smoke.cc` remains a separately labelled shared-lineage reference case.

The descriptor assembler places early-refill words in wire order, including dummy words at exact boundaries. Continuous bit packets include zero-filled read-ahead guards. SnKRLE's odd-byte-count cases test the C++ decoder; they do not claim to satisfy the assembly routine's even-count precondition.

CTest decodes every fixture directly, from an embedded source offset with trailing bytes, and into a destination with an existing prefix. Exact output length/content is checked in all cases. Consumed input is asserted only for fixtures whose public API behavior is unambiguous; broad consumption/error contracts remain part of the buffer migration work.

The manifest also lists the initial required decoder forms for each codec. The integrity check exports the compiled fixtures below the build directory, verifies both hashes, and checks that those forms still have an annotated fixture. This covers the declared forms, not all legal parameter combinations, noncanonical encodings, or encoder-selection obligations. Full format-edge inventories and encoder observers remain pending.

To review a fixture change and update the manifest explicitly:

```sh
cmake --build build-tests --target mdcomp-fixture-export
build-tests/tests/mdcomp-fixture-export build-tests/tests/fixture-export
python tests/support/update_fixture_manifest.py --export-dir build-tests/tests/fixture-export --output tests/fixtures/manifest.json
ctest --test-dir build-tests -R fixture-manifest --output-on-failure
```

For Visual Studio, build with `--config Release`, use `build-tests/tests/Release/mdcomp-fixture-export.exe`, and pass `-C Release` to CTest. Export and hash verification run through CMake in the normal suite; Python is needed only for the explicit manifest update. Fixture bytes and their expected results must be reviewed before updating checksums to accept a change.
