FROM ubuntu

RUN <<EOF
apt-get update
apt-get install -y curl build-essential python3 git cmake pkg-config busybox
EOF

ARG TARGETARCH

ENV WASI_SDK_VERSION=24
ENV WASI_SDK_PATH=/opt/wasi-sdk
ENV WASI_SDK_SYSROOT=${WASI_SDK_PATH}/share/wasi-sysroot
ENV WASI_SDK_LIBDIR=${WASI_SDK_SYSROOT}/lib/wasm32-wasi
ENV WASMTIME_VERSION=26.0.0
ENV WIZER_VERSION=7.0.5
ENV WASI_VFS_VERSION=0.5.4
ENV PYTHON_PATH=/opt/wasi-python

RUN <<EOF
mkdir ${WASI_SDK_PATH}
WASI_SDK_ARCH=$(echo ${TARGETARCH} | sed 's/amd64/x86_64/')
curl -L https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-${WASI_SDK_VERSION}/wasi-sdk-${WASI_SDK_VERSION}.0-${WASI_SDK_ARCH}-linux.tar.gz | \
    tar -xz --strip-components 1 -C ${WASI_SDK_PATH}
EOF

RUN <<EOF
WASMTIME_ARCH=$(echo ${TARGETARCH} | sed -e 's/amd64/x86_64/' -e 's/arm64/aarch64/')
WASMTIME_NAME=wasmtime-v${WASMTIME_VERSION}-${WASMTIME_ARCH}-linux
curl -L https://github.com/bytecodealliance/wasmtime/releases/download/v${WASMTIME_VERSION}/${WASMTIME_NAME}.tar.xz | \
    tar -xJ --strip-components 1 -C /usr/local/bin ${WASMTIME_NAME}/wasmtime
EOF

RUN <<EOF
WIZER_ARCH=$(echo ${TARGETARCH} | sed -e 's/amd64/x86_64/' -e 's/arm64/aarch64/')
WIZER_NAME=wizer-v${WIZER_VERSION}-${WIZER_ARCH}-linux
curl -L https://github.com/bytecodealliance/wizer/releases/download/v${WIZER_VERSION}/${WIZER_NAME}.tar.xz | \
    tar -xJ --strip-components 1 -C /usr/local/bin ${WIZER_NAME}/wizer
EOF

RUN <<EOF
WASI_VFS_ARCH=$(echo ${TARGETARCH} | sed -e 's/amd64/x86_64/' -e 's/arm64/aarch64/')
curl -L https://github.com/kateinoigakukun/wasi-vfs/releases/download/v${WASI_VFS_VERSION}/wasi-vfs-cli-${WASI_VFS_ARCH}-unknown-linux-gnu.zip | \
    busybox unzip -d /usr/local/bin -
chmod +x /usr/local/bin/wasi-vfs
curl -L https://github.com/kateinoigakukun/wasi-vfs/releases/download/v${WASI_VFS_VERSION}/libwasi_vfs-wasm32-unknown-unknown.zip | \
    busybox unzip -d ${WASI_SDK_LIBDIR} -
EOF

RUN <<EOF
mkdir -p /build/zlib
curl -L https://www.zlib.net/zlib-1.3.1.tar.gz | \
  tar -xz --strip-components 1 -C /build/zlib
EOF

RUN <<EOF
cd /build/zlib
prefix=${WASI_SDK_SYSROOT} \
libdir=${WASI_SDK_LIBDIR} \
pkgconfigdir=${WASI_SDK_SYSROOT}/lib/pkgconfig \
CC="${WASI_SDK_PATH}/bin/clang --sysroot=${WASI_SDK_SYSROOT}" \
AR="${WASI_SDK_PATH}/bin/ar" \
./configure
make install
EOF

RUN <<EOF
mkdir -p /build/cpython
curl -L https://www.python.org/ftp/python/3.13.0/Python-3.13.0.tgz | \
    tar -xz --strip-components 1 -C /build/cpython
EOF

RUN <<EOF
cd /build/cpython
python3 Tools/wasm/wasi.py configure-build-python
python3 Tools/wasm/wasi.py make-build-python
EOF

RUN <<EOF
cd /build/cpython
python3 Tools/wasm/wasi.py configure-host -- \
    CFLAGS='-Os' --prefix=${PYTHON_PATH} --disable-test-modules
python3 Tools/wasm/wasi.py make-host
make -C cross-build/wasm32-wasi install COMPILEALL_OPTS='-j0 -b'
EOF

RUN <<EOF
cd /build/cpython/cross-build/wasm32-wasi
${WASI_SDK_PATH}/bin/ar -M <<AR
create ${PYTHON_PATH}/lib/libpython3.13.a
addlib libpython3.13.a
addlib Modules/expat/libexpat.a
addlib Modules/_decimal/libmpdec/libmpdec.a
addlib Modules/_hacl/libHacl_Hash_SHA2.a
addlib ${WASI_SDK_LIBDIR}/libz.a
save
end
AR
EOF

RUN <<EOF
cd ${PYTHON_PATH}/lib/python3.13
find . -name __pycache__ -exec rm -rf {} \;
rm -rf config-3.13-wasm32-wasi
rm -rf _*_support* _pyrepl bdb concurrent curses ensurepip doctest* idlelib
rm -rf multiprocessing pdb pydoc* socketserver* sqlite3 ssl* subprocess*
rm -rf tkinter turtle* unittest venv webbrowser* wsgiref xmlrpc
EOF
