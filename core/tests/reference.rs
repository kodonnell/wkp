//! The optimised decoders against a naive byte-at-a-time decoder written straight from SPEC.md,
//! on random and adversarial input (every byte value, long continuation runs, separators in odd
//! places, truncation). Also checks that encoding round-trips through the reference decoder.

use wkp::{decode_f64, decode_geometry, encode_f64, encode_geometry, Error, Frame};

#[path = "support/spec_decoder.rs"]
mod spec_decoder;

use spec_decoder::{factor, ref_decode_f64, ref_decode_geometry};

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

/// A long, mostly well-formed body (so the SIMD decoder's 64-byte blocks get used), with
/// value lengths drawn from a per-case mix and occasional separators, long values and garbage.
fn long_body(rng: &mut Rng) -> Vec<u8> {
    let target = 80 + rng.below(1500) as usize;
    let max_short = 1 + rng.below(6);
    let noise = rng.below(4);
    let mut v = Vec::new();
    while v.len() < target {
        let len = match rng.below(100) {
            0..=1 if noise > 0 => 9 + rng.below(6), // longer than one SWAR word
            2..=5 => 5 + rng.below(4),              // 5..=8 chars
            _ => 1 + rng.below(max_short),
        };
        for _ in 1..len {
            v.push(95 + rng.below(32) as u8);
        }
        v.push(63 + rng.below(32) as u8);
        match rng.below(1000) {
            0..=2 if noise > 1 => v.push(b",;"[rng.below(2) as usize]),
            3 if noise > 2 => v.push(rng.below(256) as u8),
            4 if noise > 2 => v.truncate(v.len() - 1), // dangling continuation char
            _ => {}
        }
    }
    v
}

#[test]
fn long_inputs_match_reference_with_and_without_simd() {
    let mut rng = Rng(0x5133D);
    let (mut values, mut frame) = (Vec::new(), Frame::default());
    let mut ok = 0;
    for case in 0..6_000 {
        let body = long_body(&mut rng);
        let dims = 1 + rng.below(3) as usize;
        let precision = rng.below(8) as i32;
        let want = ref_decode_f64(&body, dims, precision).map(|v| v.iter().map(|x| x.to_bits()).collect::<Vec<_>>());
        let mut encoded = vec![64, 63 + precision as u8, 63 + dims as u8, 63 + 5];
        encoded.extend(&body);
        let want_geom = ref_decode_geometry(&encoded);
        ok += want.is_ok() as usize;
        for simd in [true, false] {
            wkp::set_simd_enabled(simd);
            let got = decode_f64(&body, dims, &[precision], &mut values)
                .map(|()| values.iter().map(|x| x.to_bits()).collect::<Vec<_>>());
            assert_eq!(got, want, "decode_f64 case {case} simd {simd} body {body:?}");
            let got = decode_geometry(&encoded, &mut frame).map(|()| {
                (
                    frame.coords.iter().map(|x| x.to_bits()).collect(),
                    frame.segment_point_counts.clone(),
                    frame.group_segment_counts.clone(),
                )
            });
            assert_eq!(got, want_geom, "decode_geometry case {case} simd {simd}");
        }
    }
    wkp::set_simd_enabled(true);
    assert!(ok > 1000, "{ok}");
}

/// The vectorised encoder against the scalar one (identical bytes and errors): dims 1..=16,
/// per-dimension precisions, exact rounding ties, values either side of the vector path's 2^50
/// limit, and NaN / infinity / out-of-range values mid-input.
#[test]
fn simd_encoder_matches_scalar() {
    let mut rng = Rng(0xE4C5);
    let uniform = |rng: &mut Rng| rng.next() as f64 / u64::MAX as f64;
    let (mut with, mut without) = (Vec::new(), Vec::new());
    let mut ok = 0;
    for case in 0..5000 {
        let dims = [1, 2, 2, 3, 3, 3, 4, 7, 16][rng.below(9) as usize];
        let n = 1 + rng.below(300) as usize;
        let ties = rng.below(4) == 0;
        let precisions: Vec<i32> = if ties {
            vec![0]
        } else if rng.below(2) == 0 {
            (0..dims).map(|_| rng.below(10) as i32).collect()
        } else {
            vec![rng.below(10) as i32]
        };
        let step = [1e-4, 1e-2, 1.0, 1e3][rng.below(4) as usize];
        let mut coords = Vec::with_capacity(n * dims);
        let mut pos = vec![0.0; dims];
        for _ in 0..n {
            for p in pos.iter_mut() {
                *p += (uniform(&mut rng) - 0.5) * step;
                coords.push(if ties { (*p * 100.0).round() + 0.5 } else { *p });
            }
        }
        // Occasionally: values around 2^50 after scaling (vector limit), up to just under 2^58
        // (still valid, scalar only), and invalid ones.
        for _ in 0..rng.below(3) {
            let i = rng.below(coords.len() as u64) as usize;
            let p = *precisions.get(i % dims).unwrap_or(&precisions[0]);
            let f = factor(p);
            coords[i] = match rng.below(6) {
                0 => (1u64 << 50) as f64 / f,
                1 => ((1u64 << 50) as f64 - 0.5) / f,
                2 => -((1u64 << 51) as f64) / f,
                3 => ((1u64 << 57) as f64) / f,
                4 => [f64::NAN, f64::INFINITY, f64::NEG_INFINITY][rng.below(3) as usize],
                _ => 1e300,
            };
        }
        wkp::set_simd_enabled(true);
        let a = encode_f64(&coords, dims, &precisions, &mut with);
        wkp::set_simd_enabled(false);
        let b = encode_f64(&coords, dims, &precisions, &mut without);
        assert_eq!(
            (a, &with),
            (b, &without),
            "encode_f64 case {case} dims {dims} {precisions:?}"
        );
        ok += a.is_ok() as usize;

        let cut = 1 + rng.below(n as u64) as usize;
        let segments: Vec<usize> = if cut < n { vec![cut, n - cut] } else { vec![n] };
        let groups = [segments.len()];
        let p = precisions[0];
        wkp::set_simd_enabled(true);
        let a = encode_geometry(3, &coords, dims, p, &groups, &segments, &mut with);
        wkp::set_simd_enabled(false);
        let b = encode_geometry(3, &coords, dims, p, &groups, &segments, &mut without);
        assert_eq!((a, &with), (b, &without), "encode_geometry case {case} dims {dims}");
    }
    wkp::set_simd_enabled(true);
    assert!(ok > 2500, "{ok}");
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
