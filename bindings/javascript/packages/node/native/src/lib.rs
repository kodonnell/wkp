//! Node-API addon for `@wkpjs/node`. `index.js` normalises arguments and builds the public API;
//! this layer only moves typed arrays in and out of `wkp`.
//!
//! Errors from invalid arguments carry `code: "InvalidArg"`, which `index.js` rethrows as
//! `TypeError`.

use std::cell::RefCell;
use std::{mem, ptr};

use napi::bindgen_prelude::*;
use napi_derive::napi;
use wkp::Frame;

thread_local! {
    // Reused output buffers (one set per JS thread). Results are copied into JS-owned memory, so
    // nothing here outlives a call.
    static BYTES: RefCell<Vec<u8>> = const { RefCell::new(Vec::new()) };
    static VALUES: RefCell<Vec<f64>> = const { RefCell::new(Vec::new()) };
    static FRAME: RefCell<Frame> = RefCell::new(Frame::default());
}

fn core_err(e: wkp::Error) -> Error {
    let status = match e {
        wkp::Error::InvalidArgument => Status::InvalidArg,
        wkp::Error::MalformedInput => Status::GenericFailure,
    };
    Error::new(status, format!("WKP core error: {e}"))
}

/// Copies `data` into a new JS-owned typed array of kind `kind`.
///
/// (`Float64ArraySlice::copy_from` and friends in napi 3.14 size the buffer in elements rather than
/// bytes and never copy the data, so they cannot be used for this.)
fn typed_array_copy<T: Copy, S: FromNapiValue>(env: &Env, data: &[T], kind: TypedArrayType) -> Result<S> {
    let bytes = mem::size_of_val(data);
    let (mut raw_data, mut buffer, mut array) = (ptr::null_mut(), ptr::null_mut(), ptr::null_mut());
    unsafe {
        check_status!(napi::sys::napi_create_arraybuffer(
            env.raw(),
            bytes,
            &mut raw_data,
            &mut buffer
        ))?;
        if bytes > 0 {
            ptr::copy_nonoverlapping(data.as_ptr().cast::<u8>(), raw_data.cast::<u8>(), bytes);
        }
        check_status!(napi::sys::napi_create_typedarray(
            env.raw(),
            kind as i32,
            data.len(),
            buffer,
            0,
            &mut array
        ))?;
        S::from_napi_value(env.raw(), array)
    }
}

fn to_usize(counts: &[u32]) -> Vec<usize> {
    counts.iter().map(|&c| c as usize).collect()
}

#[napi]
pub fn core_version() -> &'static str {
    env!("CARGO_PKG_VERSION")
}

#[napi]
pub fn run_self_test() -> Result<bool> {
    let values = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
    let mut buf = Vec::new();
    let mut frame = Frame::default();
    let ok = wkp::encode_f64(&values, 2, &[5], &mut buf).is_ok()
        && buf == b"_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::encode_geometry(2, &values, 2, 5, &[1], &[3], &mut buf).is_ok()
        && buf == b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::decode_geometry(&buf, &mut frame).is_ok()
        && frame.coords == values;
    if ok {
        Ok(true)
    } else {
        Err(Error::from_reason("WKP core self-test failed"))
    }
}

#[napi(object)]
pub struct GeometryTypes {
    #[napi(js_name = "POINT")]
    pub point: u32,
    #[napi(js_name = "LINESTRING")]
    pub linestring: u32,
    #[napi(js_name = "POLYGON")]
    pub polygon: u32,
    #[napi(js_name = "MULTIPOINT")]
    pub multipoint: u32,
    #[napi(js_name = "MULTILINESTRING")]
    pub multilinestring: u32,
    #[napi(js_name = "MULTIPOLYGON")]
    pub multipolygon: u32,
}

#[napi]
pub fn geometry_types() -> GeometryTypes {
    GeometryTypes {
        point: 1,
        linestring: 2,
        polygon: 3,
        multipoint: 4,
        multilinestring: 5,
        multipolygon: 6,
    }
}

#[napi]
pub fn encode_f64<'env>(
    env: &'env Env,
    values: &[f64],
    dimensions: u32,
    precisions: Vec<i32>,
) -> Result<BufferSlice<'env>> {
    BYTES.with_borrow_mut(|out| {
        wkp::encode_f64(values, dimensions as usize, &precisions, out).map_err(core_err)?;
        BufferSlice::copy_from(env, out.as_slice())
    })
}

#[napi]
pub fn decode_f64<'env>(
    env: &'env Env,
    encoded: &[u8],
    dimensions: u32,
    precisions: Vec<i32>,
) -> Result<Float64ArraySlice<'env>> {
    VALUES.with_borrow_mut(|out| {
        wkp::decode_f64(encoded, dimensions as usize, &precisions, out).map_err(core_err)?;
        typed_array_copy(env, out, TypedArrayType::Float64)
    })
}

#[napi]
pub fn encode_geometry_frame_f64<'env>(
    env: &'env Env,
    geometry_type: u32,
    coords: &[f64],
    dimensions: u32,
    precision: i32,
    group_segment_counts: &[u32],
    segment_point_counts: &[u32],
) -> Result<BufferSlice<'env>> {
    let geometry_type = u8::try_from(geometry_type).map_err(|_| core_err(wkp::Error::InvalidArgument))?;
    let groups = to_usize(group_segment_counts);
    let segments = to_usize(segment_point_counts);
    BYTES.with_borrow_mut(|out| {
        wkp::encode_geometry(
            geometry_type,
            coords,
            dimensions as usize,
            precision,
            &groups,
            &segments,
            out,
        )
        .map_err(core_err)?;
        BufferSlice::copy_from(env, out.as_slice())
    })
}

/// Returns `[version, precision, dimensions, geometryType]`.
#[napi]
pub fn decode_geometry_header(encoded: &[u8]) -> Result<Vec<u32>> {
    let h = wkp::decode_header(encoded).map_err(core_err)?;
    Ok(vec![
        h.version as u32,
        h.precision as u32,
        h.dimensions as u32,
        h.geometry_type as u32,
    ])
}

/// Returns `[version, precision, dimensions, geometryType, coords, segmentPointCounts,
/// groupSegmentCounts]` with typed-array copies of the decoded frame.
#[napi]
#[allow(clippy::type_complexity)]
pub fn decode_geometry_frame_flat<'env>(
    env: &'env Env,
    encoded: &[u8],
) -> Result<(
    u32,
    u32,
    u32,
    u32,
    Float64ArraySlice<'env>,
    Uint32ArraySlice<'env>,
    Uint32ArraySlice<'env>,
)> {
    FRAME.with_borrow_mut(|f| {
        wkp::decode_geometry(encoded, f).map_err(core_err)?;
        let segs: Vec<u32> = f.segment_point_counts.iter().map(|&n| n as u32).collect();
        let groups: Vec<u32> = f.group_segment_counts.iter().map(|&n| n as u32).collect();
        let h = f.header;
        Ok((
            h.version as u32,
            h.precision as u32,
            h.dimensions as u32,
            h.geometry_type as u32,
            typed_array_copy(env, &f.coords, TypedArrayType::Float64)?,
            typed_array_copy(env, &segs, TypedArrayType::Uint32)?,
            typed_array_copy(env, &groups, TypedArrayType::Uint32)?,
        ))
    })
}
