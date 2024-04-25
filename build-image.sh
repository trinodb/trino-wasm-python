#!/usr/bin/env bash

set -eu
cd "${BASH_SOURCE%/*}"

BUILD_IMAGE=${BUILD_IMAGE:-trinodb/wasm-python}

docker build . --load --tag $BUILD_IMAGE
