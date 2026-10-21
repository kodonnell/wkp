//! The optimised decoders against a naive byte-at-a-time decoder written straight from SPEC.md,
//! on random and adversarial input (every byte value, long continuation runs, separators in odd
//! places, truncation). Also checks that encoding round-trips through the reference decoder.

use wkp_core::{decode_f64, decode_geometry, encode_f64, encode_geometry, Error, Frame};

/// splitmix64.
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }
    fn below(&mut self, n: u64) -> u64 {
        self.next() % n
    }
}

fn factor(p: i32) -> f64 {
    (0..p).fold(1.0, |v, _| v * 10.0)
}

/// Reads one varint per SPEC.md section 2.4: chars 63..=126, at most 12 chars.
fn ref_varint(src: &[u8], pos: &mut usize) -> Result<i64, Error> {
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

fn ref_decode_f64(src: &[u8], dims: usize, precision: i32) -> Result<Vec<f64>, Error> {
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

type RefFrame = (Vec<u64>, Vec<usize>, Vec<usize>);

fn ref_decode_geometry(src: &[u8]) -> Result<RefFrame, Error> {
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

/// A random string biased towards interesting shapes: valid-looking varints of every length
/// (including over-long ones), separators, and arbitrary bytes.
fn random_body(rng: &mut Rng) -> Vec<u8> {
    let mut v = Vec::new();
    for _ in 0..rng.below(40) {
        match rng.below(10) {
            0..=4 => {
                // A varint of 1..=14 chars: continuation chars then a terminator.
                for _ in 0..rng.below(14) {
                    v.push(95 + rng.below(32) as u8);
                }
                v.push(63 + rng.below(32) as u8);
            }
            5 => v.push(b",;"[rng.below(2) as usize]),
            6 => v.push(rng.below(256) as u8),
            7 => v.extend(std::iter::repeat_n(95 + rng.below(32) as u8, rng.below(20) as usize)),
            _ => v.push(63 + rng.below(64) as u8),
        }
    }
    v
}

#[test]
fn decoders_match_reference_on_random_input() {
    let mut rng = Rng(0xDEC0DE);
    let (mut values, mut frame) = (Vec::new(), Frame::default());
    let mut ok_counts = [0usize; 2];
    for case in 0..60_000 {
        let body = random_body(&mut rng);
        let dims = 1 + rng.below(5) as usize;
        let precision = rng.below(12) as i32;

        let got = decode_f64(&body, dims, &[precision], &mut values).map(|()| values.clone());
        let want = ref_decode_f64(&body, dims, precision);
        let bits = |r: &Result<Vec<f64>, Error>| r.clone().map(|v| v.iter().map(|x| x.to_bits()).collect::<Vec<_>>());
        assert_eq!(
            bits(&got),
            bits(&want),
            "decode_f64 case {case} dims {dims} body {body:?}"
        );
        ok_counts[0] += got.is_ok() as usize;

        let mut encoded = vec![64, 63 + precision as u8, 63 + dims as u8, 63 + 1 + rng.below(6) as u8];
        encoded.extend(&body);
        let got = decode_geometry(&encoded, &mut frame).map(|()| {
            let bits = frame.coords.iter().map(|x| x.to_bits()).collect();
            (
                bits,
                frame.segment_point_counts.clone(),
                frame.group_segment_counts.clone(),
            )
        });
        assert_eq!(
            got,
            ref_decode_geometry(&encoded),
            "decode_geometry case {case} input {encoded:?}"
        );
        ok_counts[1] += got.is_ok() as usize;
    }
    // Make sure the generator exercises the success paths too, not just errors.
    assert!(ok_counts.iter().all(|&n| n > 500), "{ok_counts:?}");
}

#[test]
fn encoders_round_trip_through_reference() {
    let mut rng = Rng(0xE4C0DE);
    let mut out = Vec::new();
    for case in 0..20_000 {
        let dims = 1 + rng.below(4) as usize;
        let precision = rng.below(10) as i32;
        let n = rng.below(30) as usize + 1;
        let scale = [1e-3, 1.0, 180.0, 1e6][rng.below(4) as usize];
        let coords: Vec<f64> = (0..n * dims)
            .map(|_| (rng.next() as f64 / u64::MAX as f64 - 0.5) * scale)
            .collect();
        let f = factor(precision);
        // Through i64, as the format stores integers: tiny negatives decode as +0.0, not -0.0.
        let quantized: Vec<u64> = coords
            .iter()
            .map(|v| (((v * f).round() as i64) as f64 / f).to_bits())
            .collect();

        encode_f64(&coords, dims, &[precision], &mut out).unwrap();
        let decoded = ref_decode_f64(&out, dims, precision).unwrap();
        assert_eq!(
            decoded.iter().map(|x| x.to_bits()).collect::<Vec<_>>(),
            quantized,
            "case {case}"
        );

        // Split into 1..=3 segments in one group (a polygon-like frame).
        let cut = 1 + rng.below(n as u64) as usize;
        let segments: Vec<usize> = if cut < n { vec![cut, n - cut] } else { vec![n] };
        encode_geometry(3, &coords, dims, precision, &[segments.len()], &segments, &mut out).unwrap();
        let (bits, segs, groups) = ref_decode_geometry(&out).unwrap();
        assert_eq!(
            (bits, segs, groups),
            (quantized, segments.clone(), vec![segments.len()]),
            "case {case}"
        );
    }
}
