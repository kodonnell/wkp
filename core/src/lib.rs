//! WKP - Well Known Polylines.
//!
//! Implementation of the wire format described in `SPEC.md`. Output is byte-for-byte identical
//! to the original C implementation (checked against its golden vectors in `tests/data`) for
//! all valid inputs. Differences from it are limited to inputs it handled badly: non-finite or
//! out-of-range values and precisions are rejected instead of producing garbage, chars above
//! `~` are rejected while decoding, and a bad dimension count in a geometry header is an error
//! rather than a crash.
//!
//! All encode/decode functions write into caller-provided `Vec`s, which are cleared first.
//! Reusing the same `Vec`s (or [`Frame`]) across calls avoids reallocating, which is the
//! equivalent of the C API's `wkp_context`.

#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use core::fmt;
use core::ptr;

pub const MAX_DIMS: usize = 16;
pub const GEOMETRY_VERSION: u8 = 1;
pub const SEP_RING: u8 = b',';
pub const SEP_MULTI: u8 = b';';
const HEADER_OFFSET: u8 = 63;
const HEADER_MAX: i32 = 63;

/// Largest |value * 10^precision| accepted by the encoder. Keeps every delta's zigzag value
/// under 2^60, i.e. at most 12 chars, which is the longest varint the decoder accepts.
const MAX_SCALED: f64 = (1u64 << 58) as f64;
/// Worst-case encoded chars for one value.
const MAX_VARINT_LEN: usize = 12;

#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum Error {
    InvalidArgument,
    MalformedInput,
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::InvalidArgument => f.write_str("invalid argument"),
            Error::MalformedInput => f.write_str("malformed input"),
        }
    }
}

pub type Result<T> = core::result::Result<T, Error>;

#[repr(u8)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum GeometryType {
    Point = 1,
    LineString = 2,
    Polygon = 3,
    MultiPoint = 4,
    MultiLineString = 5,
    MultiPolygon = 6,
}

impl GeometryType {
    pub fn from_code(code: u8) -> Option<Self> {
        Some(match code {
            1 => Self::Point,
            2 => Self::LineString,
            3 => Self::Polygon,
            4 => Self::MultiPoint,
            5 => Self::MultiLineString,
            6 => Self::MultiPolygon,
            _ => return None,
        })
    }
}

#[derive(Copy, Clone, Debug, Default, PartialEq, Eq)]
pub struct Header {
    pub version: u8,
    pub precision: u8,
    pub dimensions: u8,
    /// Raw type code. Codes outside 1..=6 are passed through, as in the C core.
    pub geometry_type: u8,
}

/// Decoded geometry in flat form. Reuse one across calls to avoid reallocating.
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Frame {
    pub header: Header,
    /// Point-major, dimension-interleaved coordinates.
    pub coords: Vec<f64>,
    pub segment_point_counts: Vec<usize>,
    pub group_segment_counts: Vec<usize>,
}

// ---------------------------------------------------------------------------
// Scalar helpers
// ---------------------------------------------------------------------------

/// `10^precision` computed the same way as the C core (repeated multiplication), so
/// factors match bit-for-bit even where `powi` would differ.
fn factor_for_precision(precision: i32) -> f64 {
    let mut v = 1.0;
    for _ in 0..precision {
        v *= 10.0;
    }
    v
}

/// `llround` (round half away from zero) using only SSE2-level ops, so it inlines on
/// baseline x86-64 instead of calling libm. Exact for |x| < 2^63.
#[inline(always)]
fn round_half_away(x: f64) -> i64 {
    // Callers guarantee |x| < 2^58 (and not NaN), so the unchecked conversion is sound and
    // avoids the saturation/NaN fix-up code of `as i64`.
    let t: i64 = unsafe { x.to_int_unchecked() };
    let frac = x - t as f64;
    t + (frac >= 0.5) as i64 - (frac <= -0.5) as i64
}

#[inline(always)]
fn zigzag(delta: i64) -> u64 {
    ((delta << 1) ^ (delta >> 63)) as u64
}

#[inline(always)]
fn unzigzag(u: u64) -> i64 {
    ((u >> 1) as i64) ^ -((u & 1) as i64)
}

// ---------------------------------------------------------------------------
// Varint encode
//
// A value is emitted as 5-bit groups, least significant first, each as
// `group | 0x20 (if more follow)` + 63. For values that fit in 8 groups (40 bits, which is
// nearly every delta in practice) we build all output bytes in one u64 and store it with a
// single unaligned write: spread the 5-bit groups into bytes, OR in the continuation bits,
// then add 63 to every byte (no carries, since each byte is <= 63).
// ---------------------------------------------------------------------------

const LO: u64 = 0x0101_0101_0101_0101;
const HI: u64 = 0x8080_8080_8080_8080;
const CONT: u64 = 0x20 * LO;
const GROUP: u64 = 0x1F * LO;
const BIAS: u64 = 63 * LO;

/// Moves 5-bit group `i` of a 40-bit value to bits `8i..8i+5`.
#[inline(always)]
fn spread5(x: u64) -> u64 {
    let x = (x & 0x0000_0000_000F_FFFF) | ((x & 0x0000_00FF_FFF0_0000) << 12);
    let x = (x & 0x0000_03FF_0000_03FF) | ((x & 0x000F_FC00_000F_FC00) << 6);
    (x & 0x001F_001F_001F_001F) | ((x & 0x03E0_03E0_03E0_03E0) << 3)
}

/// Inverse of [`spread5`].
#[inline(always)]
fn gather5(x: u64) -> u64 {
    let x = (x & 0x001F_001F_001F_001F) | ((x >> 3) & 0x03E0_03E0_03E0_03E0);
    let x = (x & 0x0000_03FF_0000_03FF) | ((x >> 6) & 0x000F_FC00_000F_FC00);
    (x & 0x0000_0000_000F_FFFF) | ((x >> 12) & 0x0000_00FF_FFF0_0000)
}

/// `(chars, continuation bits)` for a value < 2^40, indexed by its leading zero count.
const VARINT_SHAPE: [(usize, u64); 64] = {
    let mut t = [(0, 0); 64];
    let mut lz: usize = 0;
    while lz < 64 {
        let len = (64 - lz).div_ceil(5);
        // Entries for len > 8 are never used: the fast path only handles values < 2^40.
        let cont = if len > 8 {
            0
        } else {
            CONT & ((1u64 << (8 * (len - 1))) - 1)
        };
        t[lz] = (len, cont);
        lz += 1;
    }
    t
};

/// Writes one zigzagged value at `dst`. Returns the number of chars written.
///
/// # Safety
/// `dst` must have at least `MAX_VARINT_LEN` writable bytes. Bytes past the returned length
/// may be clobbered.
#[inline(always)]
unsafe fn put_varint(dst: *mut u8, u: u64) -> usize {
    if u < (1 << 40) {
        let (len, cont) = VARINT_SHAPE[(u | 1).leading_zeros() as usize];
        let word = (spread5(u) | cont) + BIAS;
        ptr::write_unaligned(dst as *mut u64, word.to_le());
        len
    } else {
        put_varint_slow(dst, u)
    }
}

#[cold]
unsafe fn put_varint_slow(dst: *mut u8, mut u: u64) -> usize {
    let mut n = 0;
    while u >= 0x20 {
        *dst.add(n) = ((0x20 | (u & 0x1F)) + 63) as u8;
        n += 1;
        u >>= 5;
    }
    *dst.add(n) = (u + 63) as u8;
    n + 1
}

/// Output buffer cursor that keeps pointer/len/capacity in locals so the hot loop does not
/// reload them from the `Vec` after every byte store.
struct Sink<'a> {
    vec: &'a mut Vec<u8>,
    ptr: *mut u8,
    len: usize,
    cap: usize,
}

impl<'a> Sink<'a> {
    fn new(vec: &'a mut Vec<u8>, expected: usize) -> Self {
        vec.clear();
        vec.reserve(expected);
        let ptr = vec.as_mut_ptr();
        let cap = vec.capacity();
        Sink { vec, ptr, len: 0, cap }
    }

    #[inline(always)]
    fn ensure(&mut self, extra: usize) {
        if self.cap - self.len < extra {
            self.grow(extra);
        }
    }

    #[cold]
    #[inline(never)]
    fn grow(&mut self, extra: usize) {
        unsafe { self.vec.set_len(self.len) };
        self.vec.reserve(extra.max(self.cap));
        self.ptr = self.vec.as_mut_ptr();
        self.cap = self.vec.capacity();
    }

    #[inline(always)]
    fn push(&mut self, b: u8) {
        self.ensure(1);
        unsafe { *self.ptr.add(self.len) = b };
        self.len += 1;
    }

    fn finish(self) {
        unsafe { self.vec.set_len(self.len) };
    }
}

/// Encodes `coords` (point-major, `dims` per point) continuing from delta state `prev`.
/// Inlined into callers with a constant `dims` so the per-dimension loop unrolls and `prev`
/// stays in registers.
#[inline(always)]
fn encode_points(
    sink: &mut Sink<'_>,
    coords: &[f64],
    dims: usize,
    factors: &[f64; MAX_DIMS],
    prev: &mut [i64; MAX_DIMS],
) -> Result<()> {
    let mut in_range = true;
    // Locals, not `sink.len`: byte stores may alias the sink's fields as far as LLVM knows,
    // which would force a reload after every store.
    let mut last = *prev;
    let (mut ptr, mut len, mut cap) = (sink.ptr, sink.len, sink.cap);
    for point in coords.chunks_exact(dims) {
        if cap - len < dims * MAX_VARINT_LEN {
            sink.len = len;
            sink.grow(dims * MAX_VARINT_LEN);
            (ptr, cap) = (sink.ptr, sink.cap);
        }
        for d in 0..dims {
            let x = point[d] * factors[d];
            let ok = x.abs() < MAX_SCALED;
            in_range &= ok;
            let scaled = round_half_away(if ok { x } else { 0.0 });
            let delta = scaled.wrapping_sub(last[d]);
            last[d] = scaled;
            len += unsafe { put_varint(ptr.add(len), zigzag(delta)) };
        }
    }
    sink.len = len;
    *prev = last;
    // Checked once at the end to keep the loop branch-free; garbage written for an
    // out-of-range (or NaN) value is discarded with the error.
    if in_range {
        Ok(())
    } else {
        Err(Error::InvalidArgument)
    }
}

macro_rules! dispatch_dims {
    ($dims:expr, $f:ident($($arg:expr),*)) => {
        match $dims {
            1 => $f($($arg,)* 1),
            2 => $f($($arg,)* 2),
            3 => $f($($arg,)* 3),
            4 => $f($($arg,)* 4),
            d => $f($($arg,)* d),
        }
    };
}

fn normalize_precisions(dims: usize, precisions: &[i32]) -> Result<[f64; MAX_DIMS]> {
    if dims == 0 || dims > MAX_DIMS || precisions.iter().any(|p| !(0..=HEADER_MAX).contains(p)) {
        return Err(Error::InvalidArgument);
    }
    let mut factors = [0.0; MAX_DIMS];
    match precisions.len() {
        1 => factors[..dims].fill(factor_for_precision(precisions[0])),
        n if n == dims => {
            for (f, &p) in factors.iter_mut().zip(precisions) {
                *f = factor_for_precision(p);
            }
        }
        _ => return Err(Error::InvalidArgument),
    }
    Ok(factors)
}

/// Encodes a raw float stream (no header). `precisions` is either one value for all
/// dimensions or one per dimension, each in `0..=63`.
pub fn encode_f64(values: &[f64], dims: usize, precisions: &[i32], out: &mut Vec<u8>) -> Result<()> {
    let factors = normalize_precisions(dims, precisions)?;
    if values.len() % dims != 0 {
        return Err(Error::InvalidArgument);
    }
    #[inline(always)]
    fn run(out: &mut Vec<u8>, values: &[f64], factors: &[f64; MAX_DIMS], dims: usize) -> Result<()> {
        let mut sink = Sink::new(out, values.len() * 4 + 16);
        let mut prev = [0i64; MAX_DIMS];
        let r = encode_points(&mut sink, values, dims, factors, &mut prev);
        sink.finish();
        r
    }
    let r = dispatch_dims!(dims, run(out, values, &factors));
    if r.is_err() {
        out.clear();
    }
    r
}

/// Encodes a geometry with a `VPDT` header. Topology is given as point counts per segment
/// (ring/linestring/point) and segment counts per group (polygon/part).
pub fn encode_geometry(
    geometry_type: u8,
    coords: &[f64],
    dims: usize,
    precision: i32,
    group_segment_counts: &[usize],
    segment_point_counts: &[usize],
    out: &mut Vec<u8>,
) -> Result<()> {
    if dims == 0
        || dims > MAX_DIMS
        || coords.len() % dims != 0
        || !(0..=HEADER_MAX).contains(&precision)
        || geometry_type as i32 > HEADER_MAX
        || group_segment_counts.is_empty()
        || segment_point_counts.is_empty()
    {
        return Err(Error::InvalidArgument);
    }
    let summed_segments = group_segment_counts.iter().try_fold(0usize, |a, &b| a.checked_add(b));
    if summed_segments != Some(segment_point_counts.len()) {
        return Err(Error::InvalidArgument);
    }
    let total_points = segment_point_counts.iter().try_fold(0usize, |a, &b| a.checked_add(b));
    if total_points.and_then(|p| p.checked_mul(dims)) != Some(coords.len()) {
        return Err(Error::InvalidArgument);
    }

    let mut factors = [0.0; MAX_DIMS];
    factors[..dims].fill(factor_for_precision(precision));

    #[inline(always)]
    fn run(
        out: &mut Vec<u8>,
        header: [u8; 4],
        coords: &[f64],
        factors: &[f64; MAX_DIMS],
        groups: &[usize],
        segments: &[usize],
        dims: usize,
    ) -> Result<()> {
        let mut sink = Sink::new(out, coords.len() * 4 + segments.len() + 16);
        for b in header {
            sink.push(b);
        }
        let mut prev = [0i64; MAX_DIMS];
        let mut offset = 0;
        let mut seg_iter = segments.iter();
        let mut r = Ok(());
        for (g, &group_segments) in groups.iter().enumerate() {
            if g > 0 {
                sink.push(SEP_MULTI);
            }
            for s in 0..group_segments {
                if s > 0 {
                    sink.push(SEP_RING);
                }
                let n = seg_iter.next().unwrap() * dims;
                r = r.and(encode_points(
                    &mut sink,
                    &coords[offset..offset + n],
                    dims,
                    factors,
                    &mut prev,
                ));
                offset += n;
            }
        }
        sink.finish();
        r
    }

    let header = [
        GEOMETRY_VERSION + HEADER_OFFSET,
        precision as u8 + HEADER_OFFSET,
        dims as u8 + HEADER_OFFSET,
        geometry_type + HEADER_OFFSET,
    ];
    let r = dispatch_dims!(
        dims,
        run(
            out,
            header,
            coords,
            &factors,
            group_segment_counts,
            segment_point_counts
        )
    );
    if r.is_err() {
        out.clear();
    }
    r
}

// ---------------------------------------------------------------------------
// Varint decode
//
// Decoding runs in two passes over the output buffer:
//
// 1. `decode_deltas` turns chars into raw i64 deltas. The fast path loads 8 bytes, subtracts
//    63 from each, and finds every value terminator (byte without the continuation bit) in the
//    word at once, so several short values come out of one load without a load -> length ->
//    next load dependency chain. Words containing invalid chars (including separators) are
//    only used up to the first such char; anything else falls back to a scalar reader.
// 2. `integrate` prefix-sums the deltas per dimension and scales them to f64 in place.
// ---------------------------------------------------------------------------

/// Scalar varint reader. Accepts up to 12 chars (60 bits), like the C core.
#[cold]
#[inline(never)]
fn get_varint_slow(src: &[u8], pos: &mut usize) -> Result<u64> {
    let mut result = 0u64;
    let mut shift = 0;
    loop {
        let b = *src.get(*pos).ok_or(Error::MalformedInput)?;
        *pos += 1;
        let v = b.wrapping_sub(63);
        if v > 63 || shift >= 60 {
            return Err(Error::MalformedInput);
        }
        result |= ((v & 0x1F) as u64) << shift;
        shift += 5;
        if v < 0x20 {
            return Ok(result);
        }
    }
}

#[inline(always)]
fn is_value_char(b: u8) -> bool {
    (63..=126).contains(&b)
}

/// Decodes varints from `src[*pos..]` as raw deltas into `out[*n..]`, stopping at the end of
/// input or at a char that cannot start a value (a separator, or garbage), which is left
/// unconsumed for the caller to interpret.
///
/// # Safety
/// `out` must have room for `src.len() - *pos` more values (each value is at least one char).
#[inline(always)]
unsafe fn decode_deltas(src: &[u8], pos_out: &mut usize, out: *mut i64, n_out: &mut usize) -> Result<()> {
    // Work on locals and write back once: the i64 stores below may alias `pos_out`/`n_out` as
    // far as LLVM knows, which would put a store->load round trip on every value.
    let (mut pos, mut n) = (*pos_out, *n_out);
    let r = decode_deltas_local(src, &mut pos, out, &mut n);
    (*pos_out, *n_out) = (pos, n);
    r
}

#[inline(always)]
unsafe fn decode_deltas_local(src: &[u8], pos: &mut usize, out: *mut i64, n: &mut usize) -> Result<()> {
    let len = src.len();
    let base = src.as_ptr();
    // Bits (and count) of a value that started in an earlier block, plus where it started.
    let mut carry = 0u64;
    let mut carry_bits = 0u32;
    let mut value_start = *pos;
    loop {
        // Blocks advance a fixed 8 bytes, so the next load never waits on this block's decode.
        while *pos + 8 <= len {
            let w = u64::from_le(ptr::read_unaligned(base.add(*pos) as *const u64));
            // Per byte: high bit set iff (b & 0x7F) >= 63, and then low 7 bits = b - 63.
            let x = (w | HI).wrapping_sub(BIAS);
            let invalid = (w & HI) | (!x & HI) | (((w & !HI) + LO) & HI);
            let clean = invalid == 0;
            let mut term = !x & CONT;
            if !clean {
                // Only terminators before the first invalid byte delimit complete values.
                term &= (1u64 << (invalid.trailing_zeros() & !7)) - 1;
            }
            // All eight 5-bit groups packed contiguously: byte i is bits 5i..5i+5.
            let g = gather5(x & GROUP);
            if term == 0 {
                if !clean || carry_bits > 20 {
                    break;
                }
                carry |= g << carry_bits;
                carry_bits += 40;
                *pos += 8;
                continue;
            }
            let k = term.trailing_zeros() >> 3;
            let first_bits = 5 * (k + 1);
            if carry_bits + first_bits > 60 {
                break;
            }
            *out.add(*n) = unzigzag(carry | (g & ((1u64 << first_bits) - 1)) << carry_bits);
            *n += 1;
            term &= term - 1;
            let mut s = k + 1;
            while term != 0 {
                let e = (term.trailing_zeros() >> 3) + 1;
                *out.add(*n) = unzigzag((g >> (5 * s)) & ((1u64 << (5 * (e - s))) - 1));
                *n += 1;
                s = e;
                term &= term - 1;
            }
            if !clean {
                *pos += s as usize;
                value_start = *pos;
                break;
            }
            carry = g >> (5 * s);
            carry_bits = 5 * (8 - s);
            *pos += 8;
            value_start = *pos - (8 - s) as usize;
        }
        // Rewind to the start of any partial value and read one value with the scalar reader.
        *pos = value_start;
        carry = 0;
        carry_bits = 0;
        match src.get(*pos) {
            Some(&b) if is_value_char(b) => {
                *out.add(*n) = unzigzag(get_varint_slow(src, pos)?);
                *n += 1;
                value_start = *pos;
            }
            _ => return Ok(()),
        }
    }
}

/// Converts deltas in `buf[..n]` (stored as i64 bits) to absolute scaled f64s in place.
#[inline(always)]
unsafe fn integrate(buf: *mut f64, n: usize, factors: &[f64; MAX_DIMS], dims: usize) {
    let mut prev = [0i64; MAX_DIMS];
    let deltas = buf as *mut i64;
    let mut i = 0;
    while i < n {
        for d in 0..dims {
            prev[d] = prev[d].wrapping_add(*deltas.add(i + d));
            *buf.add(i + d) = prev[d] as f64 / factors[d];
        }
        i += dims;
    }
}

/// Decodes a raw float stream (no header) into `out` as flat interleaved values.
pub fn decode_f64(encoded: &[u8], dims: usize, precisions: &[i32], out: &mut Vec<f64>) -> Result<()> {
    let factors = normalize_precisions(dims, precisions)?;
    out.clear();
    out.reserve(encoded.len());
    let mut pos = 0;
    let mut n = 0;
    unsafe { decode_deltas(encoded, &mut pos, out.as_mut_ptr() as *mut i64, &mut n)? };
    if pos != encoded.len() || n % dims != 0 {
        return Err(Error::MalformedInput);
    }
    unsafe {
        dispatch_dims!(dims, integrate(out.as_mut_ptr(), n, &factors));
        out.set_len(n);
    }
    Ok(())
}

pub fn decode_header(encoded: &[u8]) -> Result<Header> {
    if encoded.len() < 4 {
        return Err(Error::InvalidArgument);
    }
    let mut fields = [0u8; 4];
    for (f, &c) in fields.iter_mut().zip(encoded) {
        let v = c.wrapping_sub(HEADER_OFFSET);
        if v as i32 > HEADER_MAX {
            return Err(Error::InvalidArgument);
        }
        *f = v;
    }
    if fields[0] != GEOMETRY_VERSION {
        return Err(Error::InvalidArgument);
    }
    Ok(Header {
        version: fields[0],
        precision: fields[1],
        dimensions: fields[2],
        geometry_type: fields[3],
    })
}

/// Decodes a geometry into `frame`, reusing its buffers.
pub fn decode_geometry(encoded: &[u8], frame: &mut Frame) -> Result<()> {
    let header = decode_header(encoded)?;
    frame.header = header;
    frame.coords.clear();
    frame.segment_point_counts.clear();
    frame.group_segment_counts.clear();
    let r = decode_geometry_body(encoded, header, frame);
    if r.is_err() {
        frame.coords.clear();
        frame.segment_point_counts.clear();
        frame.group_segment_counts.clear();
    }
    r
}

fn decode_geometry_body(src: &[u8], header: Header, frame: &mut Frame) -> Result<()> {
    let dims = header.dimensions as usize;
    // The C core trusts this field, so a crafted header can divide by zero or overrun its
    // stack arrays. Reject it here.
    if dims == 0 || dims > MAX_DIMS || src.len() <= 4 {
        return Err(Error::MalformedInput);
    }
    let mut factors = [0.0; MAX_DIMS];
    factors[..dims].fill(factor_for_precision(header.precision as i32));

    let Frame {
        coords,
        segment_point_counts: segs,
        group_segment_counts: groups,
        ..
    } = frame;
    coords.reserve(src.len());
    let out = coords.as_mut_ptr() as *mut i64;
    let mut pos = 4;
    let mut n = 0;
    let mut seg_start = 0;
    let mut group_segs = 0;
    loop {
        unsafe { decode_deltas(src, &mut pos, out, &mut n)? };
        let seg_values = n - seg_start;
        if seg_values == 0 || seg_values % dims != 0 {
            return Err(Error::MalformedInput);
        }
        segs.push(seg_values / dims);
        seg_start = n;
        group_segs += 1;
        match src.get(pos) {
            None => {
                groups.push(group_segs);
                break;
            }
            Some(&SEP_RING) => {}
            Some(&SEP_MULTI) => {
                groups.push(group_segs);
                group_segs = 0;
            }
            Some(_) => return Err(Error::MalformedInput),
        }
        pos += 1;
    }
    unsafe {
        dispatch_dims!(dims, integrate(coords.as_mut_ptr(), n, &factors));
        coords.set_len(n);
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::vec;

    #[test]
    fn spread_gather_roundtrip() {
        for &x in &[0u64, 1, 31, 32, 0x3FF, 0xFF_FFFF_FFFF, 0x12_3456_789A] {
            assert_eq!(gather5(spread5(x)), x);
        }
    }

    #[test]
    fn rounding_matches_llround() {
        let cases = [
            (2.5, 3),
            (-2.5, -3),
            (0.49999999999999994, 0),
            (-0.49999999999999994, 0),
            (1.5, 2),
            (-1.5, -2),
            (4503599627370497.0, 4503599627370497),
            (0.0, 0),
            (-0.0, 0),
        ];
        for (x, want) in cases {
            assert_eq!(round_half_away(x), want, "{x}");
        }
    }

    #[test]
    fn google_reference_vector() {
        let values = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
        let mut out = Vec::new();
        encode_f64(&values, 2, &[5], &mut out).unwrap();
        assert_eq!(out, b"_p~iF~ps|U_ulLnnqC_mqNvxq`@");
        let mut dec = Vec::new();
        decode_f64(&out, 2, &[5], &mut dec).unwrap();
        assert_eq!(dec, values);

        encode_geometry(2, &values, 2, 5, &[1], &[3], &mut out).unwrap();
        assert_eq!(out, b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@");
        let mut frame = Frame::default();
        decode_geometry(&out, &mut frame).unwrap();
        assert_eq!(frame.coords, values);
        assert_eq!(frame.segment_point_counts, vec![3]);
        assert_eq!(frame.group_segment_counts, vec![1]);
    }

    #[test]
    fn multilinestring_fixture() {
        let coords = [0.0, 0.0, 1.0, 1.0, 2.0, 2.0, 2.0, 2.0, 3.0, 3.0, 4.0, 5.0];
        let mut out = Vec::new();
        encode_geometry(5, &coords, 2, 5, &[1, 1], &[3, 3], &mut out).unwrap();
        assert_eq!(out, b"@DAD??_ibE_ibE_ibE_ibE;??_ibE_ibE_ibE_seK");
        let mut frame = Frame::default();
        decode_geometry(&out, &mut frame).unwrap();
        assert_eq!(frame.coords, coords);
        assert_eq!(frame.group_segment_counts, vec![1, 1]);
    }

    #[test]
    fn rejects_bad_dimension_header() {
        let mut frame = Frame::default();
        assert_eq!(decode_geometry(b"@D?A??", &mut frame), Err(Error::MalformedInput));
        assert_eq!(decode_geometry(b"@D~A??", &mut frame), Err(Error::MalformedInput));
    }

    #[test]
    fn rejects_malformed() {
        let mut frame = Frame::default();
        for bad in [
            &b"@DAA"[..],
            b"@DAA?",
            b"@DAA??;",
            b"@DAA;??",
            b"@DAA??,,??",
            b"@DAA_",
            b"@DAA\x7f?",
        ] {
            assert!(decode_geometry(bad, &mut frame).is_err(), "{bad:?}");
        }
        let mut v = Vec::new();
        assert!(decode_f64(b"???", 2, &[5], &mut v).is_err());
        assert!(encode_f64(&[f64::NAN, 0.0], 2, &[5], &mut Vec::new()).is_err());
    }

    #[test]
    fn long_varints_roundtrip() {
        let values = [1e12, -1e12, 2.8e17, -2.8e17, 0.0, 12345678.9];
        let mut out = Vec::new();
        encode_f64(&values, 1, &[0], &mut out).unwrap();
        let mut dec = Vec::new();
        decode_f64(&out, 1, &[0], &mut dec).unwrap();
        let want: Vec<f64> = values.iter().map(|v| round_half_away(*v) as f64).collect();
        assert_eq!(dec, want);
    }
}
