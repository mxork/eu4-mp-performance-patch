from alpine:3.21

run mkdir /build
workdir /build

run apk add gcc mingw-w64-gcc bash libc-dev

copy linux /build/linux
workdir /build/linux
run ./build

copy windows /build/windows
workdir /build/windows
run ./build

run mkdir -p /build/out
run cp /build/linux/libpatcher.a /build/out/libpatcher.a
run cp /build/windows/libpatcher.dll /build/out/libpatcher.dll
run cp /build/windows/libpatcher.dll.lib /build/out/libpatcher.dll.lib
