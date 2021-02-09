#!/bin/bash
rm compile_commands.json
make clean
compiledb make
sed -ri 's%"/(home|mingw64)%"C:/msys64/\1%g' compile_commands.json

