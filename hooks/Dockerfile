from debian:12-slim

run mkdir /build
workdir /build

run apt-get update && apt-get install -y gcc gcc-mingw-w64-x86-64 bash libc-dev

copy . /build
workdir /build
run ./build-linux
run ./build-windows
