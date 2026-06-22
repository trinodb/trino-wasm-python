#!/usr/bin/env bash

set -eu -o pipefail

BUILD_TYPE=$([[ "${1:-}" == "--debug" ]] && echo "Debug" || echo "Release")

WASI_SDK_PATH="${WASI_SDK_PATH:-/opt/wasi-sdk}"
WIZER="${WIZER:-/usr/local/bin/wizer}"
PYTHON_PATH="${PYTHON_PATH:-/opt/wasi-python}"

TARGET_DIR=target/wasm

export CMAKE_EXTRA_ARGS="
    -DCMAKE_BUILD_TYPE=${BUILD_TYPE}
    -DWASI_SDK_PREFIX=${WASI_SDK_PATH}
    -DCMAKE_TOOLCHAIN_FILE=${WASI_SDK_PATH}/share/cmake/wasi-sdk.cmake
    -DCMAKE_PREFIX_PATH=/opt/wasi-python"

cmake -B ${TARGET_DIR} ${CMAKE_EXTRA_ARGS} .
cmake --build ${TARGET_DIR} --verbose

rm -rf "${TARGET_DIR}"/python

cp -a ${PYTHON_PATH}/lib/python3.13 ${TARGET_DIR}/python
cp /work/trino.py ${TARGET_DIR}/python/site-packages/

wasi-vfs pack ${TARGET_DIR}/python-host.wasm \
    --dir ${TARGET_DIR}/python::/opt/wasi-python/lib/python3.13 \
    --output ${TARGET_DIR}/python-host-packed.wasm

rm -rf "${TARGET_DIR}"/empty
mkdir ${TARGET_DIR}/empty

env - TERM=dumb PYTHONDONTWRITEBYTECODE=1 ${WIZER} \
    --wasm-bulk-memory true \
    --allow-wasi \
    --wasm-multi-memory true \
    --inherit-stdio true \
    --inherit-env true \
    --init-func _start \
    --keep-init-func false \
    --mapdir /guest::${TARGET_DIR}/empty \
    -o ${TARGET_DIR}/python-host-opt.wasm \
    ${TARGET_DIR}/python-host-packed.wasm

rm -rf "${TARGET_DIR}"/python/site-packages/__pycache__
