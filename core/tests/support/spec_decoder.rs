//! A naive byte-at-a-time decoder written straight from SPEC.md, as the oracle for the
//! optimised decoders. Shared by `tests/reference.rs` and the fuzz targets (`fuzz/`).

#![allow(dead_code)] // each user takes what it needs

use wkp::Error;

pub fn factor(p: i32) -> f64 {
    (0..p).fold(1.0, |v, _| v * 10.0)
}

/// Reads one varint per SPEC.md section 2.4: chars 63..=126, at most 12 chars.
pub fn ref_varint(src: &[u8], pos: &mut usize) -> Result<i64, Error> {
    let (mut acc, mut shift) = (0u64, 0);
    loop {
        let &b = src.get(*pos).ok_or(Error::MalformedInput)?;
        if !(63..=126).contains(&b) || shift >= 60 {
            return Err(Error::MalformedInput);
        }
        *pos += 1;
        let v = (b - 63) as u64;
        acc |= (v & 0x1F) << shift;
        shift += 5;
        if v < 0x20 {
            return Ok(((acc >> 1) as i64) ^ -((acc & 1) as i64));
        }
    }
}

pub fn ref_decode_f64(src: &[u8], dims: usize, precision: i32) -> Result<Vec<f64>, Error> {
    let f = factor(precision);
    let (mut pos, mut prev, mut out) = (0, vec![0i64; dims], Vec::new());
    while pos < src.len() {
        let d = out.len() % dims;
        prev[d] = prev[d].wrapping_add(ref_varint(src, &mut pos)?);
        out.push(prev[d] as f64 / f);
    }
    if out.len() % dims != 0 {
        return Err(Error::MalformedInput);
    }
    Ok(out)
}

pub type RefFrame = (Vec<u64>, Vec<usize>, Vec<usize>);

pub fn ref_decode_geometry(src: &[u8]) -> Result<RefFrame, Error> {
    if src.len() < 4 || src[..4].iter().any(|&c| !(63..=126).contains(&c)) || src[0] != 64 {
        return Err(Error::InvalidArgument);
    }
    let (precision, dims) = ((src[1] - 63) as i32, (src[2] - 63) as usize);
    if dims == 0 || dims > 16 || src.len() == 4 {
        return Err(Error::MalformedInput);
    }
    let f = factor(precision);
    let (mut pos, mut prev) = (4, vec![0i64; dims]);
    let (mut coords, mut segs, mut groups) = (Vec::new(), Vec::new(), Vec::new());
    let (mut seg_values, mut group_segs) = (0usize, 0usize);
    loop {
        match src.get(pos) {
            Some(&b) if (63..=126).contains(&b) => {
                let d = seg_values % dims;
                prev[d] = prev[d].wrapping_add(ref_varint(src, &mut pos)?);
                coords.push((prev[d] as f64 / f).to_bits());
                seg_values += 1;
                continue;
            }
            next => {
                if seg_values == 0 || seg_values % dims != 0 {
                    return Err(Error::MalformedInput);
                }
                segs.push(seg_values / dims);
                seg_values = 0;
                group_segs += 1;
                match next {
                    None => {
                        groups.push(group_segs);
                        return Ok((coords, segs, groups));
                    }
                    Some(b',') => {}
                    Some(b';') => {
                        groups.push(group_segs);
                        group_segs = 0;
                    }
                    Some(_) => return Err(Error::MalformedInput),
                }
                pos += 1;
            }
        }
    }
}
