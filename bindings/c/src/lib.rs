//! C ABI declared in `include/wkp/core.h`, backed by `wkp`.
//!
//! A `wkp_context` is an opaque, heap-allocated Rust struct; results returned through it point
//! into its buffers.

#![allow(non_camel_case_types)]
// The safety contract for every exported function (pointer validity, context lifetime) is
// documented once, in include/wkp/core.h.
#![allow(clippy::missing_safety_doc)]

use core::ffi::c_char;
use core::{ptr, slice};

use wkp::{Error, Frame, GeometryType};

pub type wkp_status = i32;
pub const WKP_STATUS_OK: wkp_status = 0;
pub const WKP_STATUS_INVALID_ARGUMENT: wkp_status = 1;
pub const WKP_STATUS_MALFORMED_INPUT: wkp_status = 2;
pub const WKP_STATUS_INTERNAL_ERROR: wkp_status = 255;

#[repr(C)]
pub struct wkp_geometry_frame_f64 {
    pub version: i32,
    pub precision: i32,
    pub dimensions: i32,
    pub geometry_type: i32,
    pub coords: *const f64,
    pub coord_value_count: usize,
    pub segment_point_counts: *const usize,
    pub segment_count: usize,
    pub group_segment_counts: *const usize,
    pub group_count: usize,
}

/// Opaque to C. The buffers are reused across calls.
pub struct wkp_context {
    bytes: Vec<u8>,
    values: Vec<f64>,
    frame: Frame,
    /// What `wkp_decode_geometry_frame` returns a pointer to (it points into `frame`).
    frame_view: wkp_geometry_frame_f64,
}

fn status(e: Error) -> wkp_status {
    match e {
        Error::InvalidArgument => WKP_STATUS_INVALID_ARGUMENT,
        Error::MalformedInput => WKP_STATUS_MALFORMED_INPUT,
        _ => WKP_STATUS_INTERNAL_ERROR,
    }
}

/// `None` for a NULL pointer with a non-zero length; NULL is fine for an empty input.
unsafe fn slice_or_empty<'a, T>(p: *const T, len: usize) -> Option<&'a [T]> {
    if len == 0 {
        Some(&[])
    } else if p.is_null() {
        None
    } else {
        Some(slice::from_raw_parts(p, len))
    }
}

const VERSION: &str = concat!(env!("CARGO_PKG_VERSION"), "\0");

#[no_mangle]
pub extern "C" fn wkp_version() -> *const c_char {
    VERSION.as_ptr() as *const c_char
}

#[no_mangle]
pub extern "C" fn wkp_context_new() -> *mut wkp_context {
    Box::into_raw(Box::new(wkp_context {
        bytes: Vec::new(),
        values: Vec::new(),
        frame: Frame::default(),
        frame_view: wkp_geometry_frame_f64 {
            version: 0,
            precision: 0,
            dimensions: 0,
            geometry_type: 0,
            coords: ptr::null(),
            coord_value_count: 0,
            segment_point_counts: ptr::null(),
            segment_count: 0,
            group_segment_counts: ptr::null(),
            group_count: 0,
        },
    }))
}

#[no_mangle]
pub unsafe extern "C" fn wkp_context_free(ctx: *mut wkp_context) {
    if !ctx.is_null() {
        drop(Box::from_raw(ctx));
    }
}

/// Runs `f` on the context's byte buffer and returns it through `out_data`/`out_size` (never
/// NULL, even when empty: `Vec::as_ptr` is non-null).
unsafe fn encode_with(
    ctx: *mut wkp_context,
    out_data: *mut *const u8,
    out_size: *mut usize,
    f: impl FnOnce(&mut Vec<u8>) -> Result<(), wkp_status>,
) -> wkp_status {
    let (Some(ctx), false, false) = (ctx.as_mut(), out_data.is_null(), out_size.is_null()) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    match f(&mut ctx.bytes) {
        Ok(()) => {
            *out_data = ctx.bytes.as_ptr();
            *out_size = ctx.bytes.len();
            WKP_STATUS_OK
        }
        Err(s) => s,
    }
}

#[no_mangle]
pub unsafe extern "C" fn wkp_encode_f64(
    ctx: *mut wkp_context,
    values: *const f64,
    value_count: usize,
    dimensions: usize,
    precisions: *const i32,
    precision_count: usize,
    out_data: *mut *const u8,
    out_size: *mut usize,
) -> wkp_status {
    encode_with(ctx, out_data, out_size, |buf| {
        let values = slice_or_empty(values, value_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let precisions = slice_or_empty(precisions, precision_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        wkp::encode_f64(values, dimensions, precisions, buf).map_err(status)
    })
}

#[no_mangle]
pub unsafe extern "C" fn wkp_encode_geometry_frame(
    ctx: *mut wkp_context,
    geometry_type: i32,
    coords: *const f64,
    coord_value_count: usize,
    dimensions: usize,
    precision: i32,
    segment_point_counts: *const usize,
    segment_count: usize,
    group_segment_counts: *const usize,
    group_count: usize,
    out_data: *mut *const u8,
    out_size: *mut usize,
) -> wkp_status {
    encode_with(ctx, out_data, out_size, |buf| {
        let coords = slice_or_empty(coords, coord_value_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let segs = slice_or_empty(segment_point_counts, segment_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let groups = slice_or_empty(group_segment_counts, group_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let geometry_type = u8::try_from(geometry_type)
            .ok()
            .and_then(GeometryType::from_code)
            .ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        wkp::encode_geometry(geometry_type, coords, dimensions, precision, segs, groups, buf).map_err(status)
    })
}

#[no_mangle]
pub unsafe extern "C" fn wkp_decode_f64(
    ctx: *mut wkp_context,
    encoded: *const u8,
    encoded_size: usize,
    dimensions: usize,
    precisions: *const i32,
    precision_count: usize,
    out_values: *mut *const f64,
    out_size: *mut usize,
) -> wkp_status {
    let (Some(ctx), false, false) = (ctx.as_mut(), out_values.is_null(), out_size.is_null()) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    let (Some(encoded), Some(precisions)) = (
        slice_or_empty(encoded, encoded_size),
        slice_or_empty(precisions, precision_count),
    ) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    match wkp::decode_f64(encoded, dimensions, precisions, &mut ctx.values) {
        Ok(()) => {
            *out_values = ctx.values.as_ptr();
            *out_size = ctx.values.len();
            WKP_STATUS_OK
        }
        Err(e) => status(e),
    }
}

#[no_mangle]
pub unsafe extern "C" fn wkp_decode_geometry_frame(
    ctx: *mut wkp_context,
    encoded: *const u8,
    encoded_size: usize,
    out_frame: *mut *const wkp_geometry_frame_f64,
) -> wkp_status {
    let (Some(ctx), false) = (ctx.as_mut(), out_frame.is_null()) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    let Some(encoded) = slice_or_empty(encoded, encoded_size) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    if let Err(e) = wkp::decode_geometry(encoded, &mut ctx.frame) {
        return status(e);
    }
    // A decoded geometry always has at least one coordinate, segment and group.
    let f = &ctx.frame;
    ctx.frame_view = wkp_geometry_frame_f64 {
        version: f.header.version as i32,
        precision: f.header.precision as i32,
        dimensions: f.header.dimensions as i32,
        geometry_type: f.header.geometry_type.code() as i32,
        coords: f.coords.as_ptr(),
        coord_value_count: f.coords.len(),
        segment_point_counts: f.segment_point_counts.as_ptr(),
        segment_count: f.segment_point_counts.len(),
        group_segment_counts: f.group_segment_counts.as_ptr(),
        group_count: f.group_segment_counts.len(),
    };
    *out_frame = &ctx.frame_view;
    WKP_STATUS_OK
}

#[no_mangle]
pub unsafe extern "C" fn wkp_decode_geometry_header(
    encoded: *const u8,
    encoded_size: usize,
    out_version: *mut i32,
    out_precision: *mut i32,
    out_dimensions: *mut i32,
    out_geometry_type: *mut i32,
) -> wkp_status {
    if out_version.is_null() || out_precision.is_null() || out_dimensions.is_null() || out_geometry_type.is_null() {
        return WKP_STATUS_INVALID_ARGUMENT;
    }
    let Some(encoded) = slice_or_empty(encoded, encoded_size) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    match wkp::decode_header(encoded) {
        Ok(h) => {
            *out_version = h.version as i32;
            *out_precision = h.precision as i32;
            *out_dimensions = h.dimensions as i32;
            *out_geometry_type = h.geometry_type.code() as i32;
            WKP_STATUS_OK
        }
        Err(e) => status(e),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn version_header_matches_crate() {
        let header = include_str!("../include/wkp/_version.h");
        assert!(
            header.contains(&format!("#define WKP_CORE_VERSION \"{}\"", env!("CARGO_PKG_VERSION"))),
            "{header}"
        );
        let v = unsafe { core::ffi::CStr::from_ptr(wkp_version()) };
        assert_eq!(v.to_str().unwrap(), env!("CARGO_PKG_VERSION"));
    }

    #[test]
    fn empty_outputs_are_non_null() {
        unsafe {
            let ctx = wkp_context_new();
            let (mut values, mut n) = (ptr::null(), 99);
            let s = wkp_decode_f64(ctx, ptr::null(), 0, 2, [5].as_ptr(), 1, &mut values, &mut n);
            assert_eq!((s, n, values.is_null()), (WKP_STATUS_OK, 0, false));
            let (mut data, mut n) = (ptr::null(), 99);
            let s = wkp_encode_f64(ctx, ptr::null(), 0, 2, [5].as_ptr(), 1, &mut data, &mut n);
            assert_eq!((s, n, data.is_null()), (WKP_STATUS_OK, 0, false));
            wkp_context_free(ctx);
            wkp_context_free(ptr::null_mut());
        }
    }

    #[test]
    fn geometry_round_trip() {
        unsafe {
            let ctx = wkp_context_new();
            let coords = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
            let (mut data, mut n) = (ptr::null(), 0);
            let s = wkp_encode_geometry_frame(
                ctx,
                2,
                coords.as_ptr(),
                6,
                2,
                5,
                [3usize].as_ptr(),
                1,
                [1usize].as_ptr(),
                1,
                &mut data,
                &mut n,
            );
            assert_eq!(s, WKP_STATUS_OK);
            assert_eq!(slice::from_raw_parts(data, n), b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@");
            let mut frame = ptr::null();
            assert_eq!(wkp_decode_geometry_frame(ctx, data, n, &mut frame), WKP_STATUS_OK);
            let f = &*frame;
            assert_eq!((f.geometry_type, f.segment_count, f.group_count), (2, 1, 1));
            assert_eq!(slice::from_raw_parts(f.coords, f.coord_value_count), coords);
            // An unknown geometry type is an invalid argument.
            let s = wkp_encode_geometry_frame(
                ctx,
                7,
                coords.as_ptr(),
                6,
                2,
                5,
                [3usize].as_ptr(),
                1,
                [1usize].as_ptr(),
                1,
                &mut data,
                &mut n,
            );
            assert_eq!(s, WKP_STATUS_INVALID_ARGUMENT);
            wkp_context_free(ctx);
        }
    }
}
