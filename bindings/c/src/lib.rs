//! C ABI declared in `include/wkp/core.h`, backed by `wkp`.
//!
//! Context buffers are Rust `Vec`s stored as raw parts in the C struct, so a context must only
//! be initialised, used and freed through these functions.

#![allow(non_camel_case_types)]
// The safety contract for every exported function (pointer validity, context lifetime) is
// documented once, in include/wkp/core.h.
#![allow(clippy::missing_safety_doc)]

use core::ffi::c_char;
use core::mem::ManuallyDrop;
use core::{ptr, slice};

use wkp::{Error, Frame};

pub type wkp_status = i32;
pub const WKP_STATUS_OK: wkp_status = 0;
pub const WKP_STATUS_INVALID_ARGUMENT: wkp_status = 1;
pub const WKP_STATUS_MALFORMED_INPUT: wkp_status = 2;
pub const WKP_STATUS_ALLOCATION_FAILED: wkp_status = 3;
pub const WKP_STATUS_BUFFER_TOO_SMALL: wkp_status = 4;
pub const WKP_STATUS_LIMIT_EXCEEDED: wkp_status = 5;
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

#[repr(C)]
pub struct wkp_context {
    pub u8_: *mut u8,
    pub u8_cap: usize,
    pub f64_: *mut f64,
    pub f64_cap: usize,
    pub sizes_a: *mut usize,
    pub sizes_a_cap: usize,
    pub sizes_b: *mut usize,
    pub sizes_b_cap: usize,
    pub frame: wkp_geometry_frame_f64,
}

const EMPTY_FRAME: wkp_geometry_frame_f64 = wkp_geometry_frame_f64 {
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
};

fn status(e: Error) -> wkp_status {
    match e {
        Error::InvalidArgument => WKP_STATUS_INVALID_ARGUMENT,
        Error::MalformedInput => WKP_STATUS_MALFORMED_INPUT,
    }
}

/// Takes ownership of a context buffer as a `Vec` (len 0).
unsafe fn take<T>(p: &mut *mut T, cap: &mut usize) -> Vec<T> {
    let v = if p.is_null() {
        Vec::new()
    } else {
        Vec::from_raw_parts(*p, 0, *cap)
    };
    *p = ptr::null_mut();
    *cap = 0;
    v
}

/// Returns a `Vec` to the context. Its contents stay valid until the next call.
fn put<T>(v: Vec<T>, p: &mut *mut T, cap: &mut usize) {
    let mut v = ManuallyDrop::new(v);
    *cap = v.capacity();
    *p = if *cap == 0 { ptr::null_mut() } else { v.as_mut_ptr() };
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
pub unsafe extern "C" fn wkp_context_init(ctx: *mut wkp_context) -> wkp_status {
    let Some(ctx) = ctx.as_mut() else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    ptr::write(
        ctx,
        wkp_context {
            u8_: ptr::null_mut(),
            u8_cap: 0,
            f64_: ptr::null_mut(),
            f64_cap: 0,
            sizes_a: ptr::null_mut(),
            sizes_a_cap: 0,
            sizes_b: ptr::null_mut(),
            sizes_b_cap: 0,
            frame: EMPTY_FRAME,
        },
    );
    WKP_STATUS_OK
}

#[no_mangle]
pub unsafe extern "C" fn wkp_context_free(ctx: *mut wkp_context) {
    let Some(ctx) = ctx.as_mut() else { return };
    drop(take(&mut ctx.u8_, &mut ctx.u8_cap));
    drop(take(&mut ctx.f64_, &mut ctx.f64_cap));
    drop(take(&mut ctx.sizes_a, &mut ctx.sizes_a_cap));
    drop(take(&mut ctx.sizes_b, &mut ctx.sizes_b_cap));
    ctx.frame = EMPTY_FRAME;
}

unsafe fn encode_with(
    ctx: *mut wkp_context,
    out_data: *mut *const u8,
    out_size: *mut usize,
    f: impl FnOnce(&mut Vec<u8>) -> Result<(), wkp_status>,
) -> wkp_status {
    let (Some(ctx), false, false) = (ctx.as_mut(), out_data.is_null(), out_size.is_null()) else {
        return WKP_STATUS_INVALID_ARGUMENT;
    };
    let mut buf = take(&mut ctx.u8_, &mut ctx.u8_cap);
    let r = f(&mut buf);
    let len = buf.len();
    put(buf, &mut ctx.u8_, &mut ctx.u8_cap);
    match r {
        Ok(()) => {
            *out_data = ctx.u8_;
            *out_size = len;
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
    group_segment_counts: *const usize,
    group_count: usize,
    segment_point_counts: *const usize,
    segment_count: usize,
    out_data: *mut *const u8,
    out_size: *mut usize,
) -> wkp_status {
    encode_with(ctx, out_data, out_size, |buf| {
        let coords = slice_or_empty(coords, coord_value_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let groups = slice_or_empty(group_segment_counts, group_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let segs = slice_or_empty(segment_point_counts, segment_count).ok_or(WKP_STATUS_INVALID_ARGUMENT)?;
        let geometry_type = u8::try_from(geometry_type).map_err(|_| WKP_STATUS_INVALID_ARGUMENT)?;
        wkp::encode_geometry(geometry_type, coords, dimensions, precision, groups, segs, buf).map_err(status)
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
    let mut buf = take(&mut ctx.f64_, &mut ctx.f64_cap);
    // Keep the returned pointer non-NULL even for empty output, as the C core did.
    buf.reserve(1);
    let r = wkp::decode_f64(encoded, dimensions, precisions, &mut buf);
    let len = buf.len();
    put(buf, &mut ctx.f64_, &mut ctx.f64_cap);
    match r {
        Ok(()) => {
            *out_values = ctx.f64_;
            *out_size = len;
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
    let mut frame = Frame {
        header: Default::default(),
        coords: take(&mut ctx.f64_, &mut ctx.f64_cap),
        segment_point_counts: take(&mut ctx.sizes_a, &mut ctx.sizes_a_cap),
        group_segment_counts: take(&mut ctx.sizes_b, &mut ctx.sizes_b_cap),
    };
    let r = wkp::decode_geometry(encoded, &mut frame);
    let h = frame.header;
    let (nc, ns, ng) = (
        frame.coords.len(),
        frame.segment_point_counts.len(),
        frame.group_segment_counts.len(),
    );
    put(frame.coords, &mut ctx.f64_, &mut ctx.f64_cap);
    put(frame.segment_point_counts, &mut ctx.sizes_a, &mut ctx.sizes_a_cap);
    put(frame.group_segment_counts, &mut ctx.sizes_b, &mut ctx.sizes_b_cap);
    if let Err(e) = r {
        return status(e);
    }
    ctx.frame = wkp_geometry_frame_f64 {
        version: h.version as i32,
        precision: h.precision as i32,
        dimensions: h.dimensions as i32,
        geometry_type: h.geometry_type as i32,
        coords: if nc > 0 { ctx.f64_ } else { ptr::null() },
        coord_value_count: nc,
        segment_point_counts: if ns > 0 { ctx.sizes_a } else { ptr::null() },
        segment_count: ns,
        group_segment_counts: if ng > 0 { ctx.sizes_b } else { ptr::null() },
        group_count: ng,
    };
    *out_frame = &ctx.frame;
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
            *out_geometry_type = h.geometry_type as i32;
            WKP_STATUS_OK
        }
        Err(e) => status(e),
    }
}

#[no_mangle]
pub unsafe extern "C" fn wkp_basic_self_test(out_failed_check: *mut i32) -> wkp_status {
    let values = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
    let fail = |check: i32| {
        if let Some(out) = out_failed_check.as_mut() {
            *out = check;
        }
        WKP_STATUS_INTERNAL_ERROR
    };
    if let Some(out) = out_failed_check.as_mut() {
        *out = 0;
    }
    let mut buf = Vec::new();
    if wkp::encode_f64(&values, 2, &[5], &mut buf).is_err() || buf != b"_p~iF~ps|U_ulLnnqC_mqNvxq`@" {
        return fail(2);
    }
    let mut decoded = Vec::new();
    if wkp::decode_f64(&buf, 2, &[5], &mut decoded).is_err() || decoded != values {
        return fail(5);
    }
    if wkp::encode_geometry(2, &values, 2, 5, &[1], &[3], &mut buf).is_err()
        || buf != b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@"
    {
        return fail(8);
    }
    let mut frame = Frame::default();
    if wkp::decode_geometry(&buf, &mut frame).is_err() || frame.coords != values {
        return fail(13);
    }
    WKP_STATUS_OK
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
    fn self_test_passes() {
        let mut failed = -1;
        assert_eq!(unsafe { wkp_basic_self_test(&mut failed) }, WKP_STATUS_OK);
        assert_eq!(failed, 0);
    }

    #[test]
    fn empty_outputs_are_non_null() {
        unsafe {
            let mut ctx: wkp_context = core::mem::zeroed();
            assert_eq!(wkp_context_init(&mut ctx), WKP_STATUS_OK);
            let (mut values, mut n) = (ptr::null(), 99);
            let s = wkp_decode_f64(&mut ctx, ptr::null(), 0, 2, [5].as_ptr(), 1, &mut values, &mut n);
            assert_eq!((s, n, values.is_null()), (WKP_STATUS_OK, 0, false));
            let (mut data, mut n) = (ptr::null(), 99);
            let s = wkp_encode_f64(&mut ctx, ptr::null(), 0, 2, [5].as_ptr(), 1, &mut data, &mut n);
            assert_eq!((s, n, data.is_null()), (WKP_STATUS_OK, 0, false));
            wkp_context_free(&mut ctx);
        }
    }
}
