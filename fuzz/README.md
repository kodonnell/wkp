# Fuzzing

Differential fuzz targets for the Rust core, built with
[cargo-fuzz](https://github.com/rust-fuzz/cargo-fuzz) (libFuzzer + AddressSanitizer):

- `decode`: arbitrary bytes are decoded as a raw float stream and as a geometry, with the SIMD
  paths on and off. The results must match the naive decoder in
  `core/tests/support/spec_decoder.rs` exactly: values bit for bit, errors by kind. ASan also
  catches out-of-bounds reads by the block-wise decoders.
- `encode`: arbitrary f64s (any bit pattern, or path-like steps) are encoded with the SIMD paths
  on and off. Both must give identical bytes and errors. Anything encoded must decode, through
  the naive decoder, to the input rounded to the precision.

## Running

Needs a nightly toolchain and cargo-fuzz (`rustup toolchain install nightly`,
`cargo install cargo-fuzz`). On Linux or macOS:

```sh
cd fuzz
cargo +nightly fuzz run decode -- -max_total_time=300
cargo +nightly fuzz run encode -- -max_total_time=300
```

A failing input is saved under `artifacts/<target>/`. Replay it with
`cargo +nightly fuzz run <target> artifacts/<target>/<file>`, then add it as a regression test
in `core/tests/`.

CI (`.github/workflows/fuzz.yml`) runs each target for 2 minutes on every push and pull request,
and for 30 minutes weekly. It keeps the corpus in the Actions cache.
