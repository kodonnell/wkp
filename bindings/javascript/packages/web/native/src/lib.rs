//! WebAssembly exports for `@wkpjs/web` (built for `wasm32-unknown-unknown`, no JS glue).
//!
//! Protocol, per context:
//! 1. `wkp_ctx_input(ctx, bytes)` returns an 8-byte-aligned scratch region of at least `bytes`;
//!    JS copies inputs (coords, counts, precisions) into it.
//! 2. An encode/decode call returns a status (0 = ok, as in the C ABI) and leaves its result in
//!    the context, described by the `Output` struct at `wkp_ctx_output(ctx)`.
//! 3. JS copies the result out before the next call on that context.
//!
//! Any call may grow linear memory, so JS must re-create its views of `memory.buffer` after
//! each call.

use wkp::{Error, Frame, GeometryType};

const STATUS_OK: i32 = 0;

fn status(r: Result<(), Error>) -> i32 {
    match r {
        Ok(()) => STATUS_OK,
        Err(Error::InvalidArgument) => 1,
        Err(Error::MalformedInput) => 2,
        Err(_) => 255,
    }
}

/// Result description read by JS (all fields are 32-bit on wasm32).
#[repr(C)]
#[derive(Default)]
pub struct Output {
    /// Encoded bytes (encode) or decoded f64 values (decode).
    data_ptr: usize,
    data_len: usize,
    segments_ptr: usize,
    segments_len: usize,
    groups_ptr: usize,
    groups_len: usize,
    version: u32,
    precision: u32,
    dimensions: u32,
    geometry_type: u32,
}

pub struct Context {
    input: Vec<u64>,
    bytes: Vec<u8>,
    values: Vec<f64>,
    frame: Frame,
    output: Output,
}

/// # Safety
/// `ptr` must be null or valid for `len` elements.
unsafe fn slice<'a, T>(ptr: *const T, len: usize) -> &'a [T] {
    if len == 0 || ptr.is_null() {
        &[]
    } else {
        core::slice::from_raw_parts(ptr, len)
    }
}

#[no_mangle]
pub extern "C" fn wkp_ctx_new() -> *mut Context {
    Box::into_raw(Box::new(Context {
        input: Vec::new(),
        bytes: Vec::new(),
        values: Vec::new(),
        frame: Frame::default(),
        output: Output::default(),
    }))
}

/// # Safety
/// `ctx` must come from `wkp_ctx_new` and not be used afterwards.
#[no_mangle]
pub unsafe extern "C" fn wkp_ctx_free(ctx: *mut Context) {
    if !ctx.is_null() {
        drop(Box::from_raw(ctx));
    }
}

/// # Safety
/// `ctx` must come from `wkp_ctx_new`.
#[no_mangle]
pub unsafe extern "C" fn wkp_ctx_input(ctx: *mut Context, bytes: usize) -> *mut u8 {
    let ctx = &mut *ctx;
    let words = bytes.div_ceil(8);
    if ctx.input.len() < words {
        ctx.input.resize(words, 0);
    }
    ctx.input.as_mut_ptr().cast()
}

/// # Safety
/// `ctx` must come from `wkp_ctx_new`.
#[no_mangle]
pub unsafe extern "C" fn wkp_ctx_output(ctx: *mut Context) -> *const Output {
    &(*ctx).output
}

fn set_bytes_output(ctx: &mut Context) {
    ctx.output = Output {
        data_ptr: ctx.bytes.as_ptr() as usize,
        data_len: ctx.bytes.len(),
        ..Output::default()
    };
}

/// # Safety
/// Pointers must be valid for their counts (precisions as i32).
#[no_mangle]
pub unsafe extern "C" fn wkp_encode_f64(
    ctx: *mut Context,
    values: *const f64,
    value_count: usize,
    dimensions: usize,
    precisions: *const i32,
    precision_count: usize,
) -> i32 {
    let ctx = &mut *ctx;
    let r = wkp::encode_f64(
        slice(values, value_count),
        dimensions,
        slice(precisions, precision_count),
        &mut ctx.bytes,
    );
    set_bytes_output(ctx);
    status(r)
}

/// # Safety
/// Pointers must be valid for their counts (counts as u32/usize).
#[no_mangle]
pub unsafe extern "C" fn wkp_encode_geometry(
    ctx: *mut Context,
    geometry_type: u32,
    coords: *const f64,
    coord_count: usize,
    dimensions: usize,
    precision: i32,
    segment_point_counts: *const usize,
    segment_count: usize,
    group_segment_counts: *const usize,
    group_count: usize,
) -> i32 {
    let ctx = &mut *ctx;
    let Some(geometry_type) = u8::try_from(geometry_type).ok().and_then(GeometryType::from_code) else {
        return status(Err(Error::InvalidArgument));
    };
    let r = wkp::encode_geometry(
        geometry_type,
        slice(coords, coord_count),
        dimensions,
        precision,
        slice(segment_point_counts, segment_count),
        slice(group_segment_counts, group_count),
        &mut ctx.bytes,
    );
    set_bytes_output(ctx);
    status(r)
}

/// # Safety
/// Pointers must be valid for their counts.
#[no_mangle]
pub unsafe extern "C" fn wkp_decode_f64(
    ctx: *mut Context,
    encoded: *const u8,
    encoded_len: usize,
    dimensions: usize,
    precisions: *const i32,
    precision_count: usize,
) -> i32 {
    let ctx = &mut *ctx;
    let r = wkp::decode_f64(
        slice(encoded, encoded_len),
        dimensions,
        slice(precisions, precision_count),
        &mut ctx.values,
    );
    ctx.output = Output {
        data_ptr: ctx.values.as_ptr() as usize,
        data_len: ctx.values.len(),
        ..Output::default()
    };
    status(r)
}

/// # Safety
/// `encoded` must be valid for `encoded_len` bytes.
#[no_mangle]
pub unsafe extern "C" fn wkp_decode_geometry(ctx: *mut Context, encoded: *const u8, encoded_len: usize) -> i32 {
    let ctx = &mut *ctx;
    let r = wkp::decode_geometry(slice(encoded, encoded_len), &mut ctx.frame);
    let f = &ctx.frame;
    ctx.output = Output {
        data_ptr: f.coords.as_ptr() as usize,
        data_len: f.coords.len(),
        segments_ptr: f.segment_point_counts.as_ptr() as usize,
        segments_len: f.segment_point_counts.len(),
        groups_ptr: f.group_segment_counts.as_ptr() as usize,
        groups_len: f.group_segment_counts.len(),
        version: f.header.version as u32,
        precision: f.header.precision as u32,
        dimensions: f.header.dimensions as u32,
        geometry_type: f.header.geometry_type.code() as u32,
    };
    status(r)
}

/// Writes `[version, precision, dimensions, geometry_type]` to `out` (4 x u32) on success.
///
/// # Safety
/// `encoded` must be valid for `encoded_len` bytes and `out` for 4 u32s.
#[no_mangle]
pub unsafe extern "C" fn wkp_decode_header(encoded: *const u8, encoded_len: usize, out: *mut u32) -> i32 {
    match wkp::decode_header(slice(encoded, encoded_len)) {
        Ok(h) => {
            let fields = [h.version, h.precision, h.dimensions, h.geometry_type.code()].map(u32::from);
            core::ptr::copy_nonoverlapping(fields.as_ptr(), out, 4);
            STATUS_OK
        }
        Err(e) => status(Err(e)),
    }
}

const VERSION: &str = env!("CARGO_PKG_VERSION");

#[no_mangle]
pub extern "C" fn wkp_version_ptr() -> *const u8 {
    VERSION.as_ptr()
}

#[no_mangle]
pub extern "C" fn wkp_version_len() -> usize {
    VERSION.len()
}

#[no_mangle]
pub extern "C" fn wkp_self_test() -> i32 {
    let values = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
    let mut buf = Vec::new();
    let mut frame = Frame::default();
    let ok = wkp::encode_f64(&values, 2, &[5], &mut buf).is_ok()
        && buf == b"_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::encode_geometry(GeometryType::LineString, &values, 2, 5, &[3], &[1], &mut buf).is_ok()
        && buf == b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::decode_geometry(&buf, &mut frame).is_ok()
        && frame.coords == values;
    if ok {
        STATUS_OK
    } else {
        255
    }
}
