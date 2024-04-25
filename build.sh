#!/usr/bin/env bash

set -eu
cd "${BASH_SOURCE%/*}"

BUILD_IMAGE=${BUILD_IMAGE:-trinodb/wasm-python}

docker run -t -v"$PWD":/work -w /work $BUILD_IMAGE ./build-wasm.sh "$@"
