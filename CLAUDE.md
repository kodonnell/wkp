# WKP - well known polylines

- Purpose is to create a compact string-based geometry encoding format inspired by Google Polyline, but extended for configurable precision, arbitrary dimensions (for example XYZ), polygon and multi-geometry support.
- Performance is a key focus - this is expected to be a backbone of other libraries, so needs to be fast.
- Design principle is to have a shared core library for all the common work (Rust, in `core/`), and then various languages wrap this (pyo3, napi-rs, WebAssembly, and a C ABI in `bindings/c` for C/C++/C#). Preference is for the languages to have as similar an API as possible.
- `micromamba activate wkp` gives cmake and python etc. Rust comes from rustup (`cargo` must be on PATH).