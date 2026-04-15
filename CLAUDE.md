# WKP - well known polylines

- Purpose is to create a compact string-based geometry encoding format inspired by Google Polyline, but extended for configurable precision, arbitrary dimensions (for example XYZ), polygon and multi-geometry support.
- Performance is a key focus - this is expected to be a backbone of other libraries, so needs to be fast.
- Design principle is to try to have a shared C library for all the common work, and then various languages wrap this. Preference is for the languages to have as similar an API as possible.
- `micromamba activate wkp` gives cmake and python and emcc etc.