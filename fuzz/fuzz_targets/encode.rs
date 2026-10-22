//! Differential fuzzing of the encoders: arbitrary f64s (any bit pattern: NaN, infinities,
//! subnormals, huge values) encoded with the SIMD paths on and off must give identical bytes and
//! errors, and anything encoded must decode, through the naive SPEC.md decoder, to the input
//! rounded to the precision.
#![no_main]

use libfuzzer_sys::fuzz_target;
use wkp::{encode_f64, encode_geometry, set_simd_enabled, GeometryType};

#[path = "../../core/tests/support/spec_decoder.rs"]
mod spec_decoder;

use spec_decoder::{factor, ref_decode_f64, ref_decode_geometry};

fuzz_target!(|data: &[u8]| {
    let Some((cfg, rest)) = data.split_first_chunk::<2>() else {
        return;
    };
    let dims = 1 + (cfg[0] & 3) as usize;
    let precision = ((cfg[0] >> 2) % 16) as i32;
    let mut values: Vec<f64> = rest
        .chunks_exact(8)
        .map(|b| f64::from_le_bytes(b.try_into().unwrap()))
        .collect();
    values.truncate(values.len() - values.len() % dims);
    if values.is_empty() {
        return;
    }
    // Mostly path-like data, to reach the paths that real input does: with bit 0 of the second
    // config byte set, each value is a small step from the previous one in its dimension.
    if cfg[1] & 1 != 0 {
        for i in dims..values.len() {
            let step = values[i];
            values[i] = values[i - dims] + if step.is_finite() { step % 1.0 } else { step };
        }
    }

    let mut out = [Vec::new(), Vec::new()];
    let mut results = [Ok(()), Ok(())];
    for (k, simd) in [true, false].into_iter().enumerate() {
        set_simd_enabled(simd);
        results[k] = encode_f64(&values, dims, &[precision], &mut out[k]);
    }
    assert_eq!(
        (&results[0], &out[0]),
        (&results[1], &out[1]),
        "encode_f64 dims={dims} p={precision}"
    );

    if results[0].is_ok() {
        let f = factor(precision);
        // Through i64, as the format stores integers (so -0.0 and tiny negatives give +0.0).
        let want: Vec<u64> = values
            .iter()
            .map(|v| (((v * f).round() as i64) as f64 / f).to_bits())
            .collect();
        let got = ref_decode_f64(&out[0], dims, precision).expect("reference decode of encoder output");
        assert_eq!(
            got.iter().map(|x| x.to_bits()).collect::<Vec<_>>(),
            want,
            "round trip dims={dims} p={precision}"
        );
    }

    // As a geometry, split into two rings of one polygon when there are enough points.
    let points = values.len() / dims;
    let cut = 1 + cfg[1] as usize % points;
    let segments: Vec<usize> = if cut < points {
        vec![cut, points - cut]
    } else {
        vec![points]
    };
    let groups = [segments.len()];
    for (k, simd) in [true, false].into_iter().enumerate() {
        set_simd_enabled(simd);
        results[k] = encode_geometry(GeometryType::Polygon, &values, dims, precision, &segments, &groups, &mut out[k]);
    }
    assert_eq!(
        (&results[0], &out[0]),
        (&results[1], &out[1]),
        "encode_geometry dims={dims} p={precision}"
    );
    if results[0].is_ok() {
        let (_, segs, grps) = ref_decode_geometry(&out[0]).expect("reference decode of encoder output");
        assert_eq!((segs, grps.to_vec()), (segments, groups.to_vec()));
    }
});
