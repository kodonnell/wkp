//! Differential fuzzing of the decoders: arbitrary bytes, decoded by `wkp` with the SIMD paths on
//! and off, must give exactly what the naive SPEC.md decoder gives (values compared bit for bit,
//! errors compared by kind). Built with AddressSanitizer, which also catches out-of-bounds reads
//! by the block-wise decoders.
#![no_main]

use libfuzzer_sys::fuzz_target;
use wkp::{decode_f64, decode_geometry, set_simd_enabled, Frame};

#[path = "../../core/tests/support/spec_decoder.rs"]
mod spec_decoder;

use spec_decoder::{ref_decode_f64, ref_decode_geometry};

fuzz_target!(|data: &[u8]| {
    let Some((&cfg, body)) = data.split_first() else {
        return;
    };
    // Raw float stream: dims 1-4 and precision 0-11 from the first byte, the rest as input.
    let dims = 1 + (cfg & 3) as usize;
    let precision = ((cfg >> 2) % 12) as i32;
    let want = ref_decode_f64(body, dims, precision).map(|v| v.iter().map(|x| x.to_bits()).collect::<Vec<_>>());
    // Geometry: the whole input, header included.
    let want_geom = ref_decode_geometry(data);

    let (mut values, mut frame) = (Vec::new(), Frame::default());
    for simd in [true, false] {
        set_simd_enabled(simd);
        let got = decode_f64(body, dims, &[precision], &mut values)
            .map(|()| values.iter().map(|x| x.to_bits()).collect::<Vec<_>>());
        assert_eq!(got, want, "decode_f64 simd={simd} dims={dims} p={precision}");

        let got = decode_geometry(data, &mut frame).map(|()| {
            (
                frame.coords.iter().map(|x| x.to_bits()).collect::<Vec<_>>(),
                frame.segment_point_counts.clone(),
                frame.group_segment_counts.clone(),
            )
        });
        assert_eq!(got, want_geom, "decode_geometry simd={simd}");
    }
});
