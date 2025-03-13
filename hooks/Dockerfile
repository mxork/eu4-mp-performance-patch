from alpine:3.21

run mkdir /build
workdir /build

run apk add gcc mingw-w64-gcc bash libc-dev

copy . /build
workdir /build
run ./build-linux
run ./build-windows
