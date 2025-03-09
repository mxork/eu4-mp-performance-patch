from rust:1.85-slim as builder

run mkdir /build
workdir /build

copy Cargo.toml /build
copy Cargo.lock /build
run cargo fetch

run apt-get update && apt-get install -y protobuf-compiler

copy vendor /build/vendor
copy build.rs /build/build.rs
copy src /build/src
run cargo build --release

run mkdir /build/out
run cp /build/target/release/runk /build/out/runk

from debian:bookworm-slim
copy --from=builder /build/out/runk /runk
env RUST_LISTEN_ADDR="0.0.0.0:7350"
entrypoint ["/runk"]
