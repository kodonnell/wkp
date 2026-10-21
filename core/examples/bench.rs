//! Core encode/decode benchmark.
//!
//! Usage: cargo run --release -p wkp-core --example bench [rounds]

use std::hint::black_box;
use std::path::Path;
use std::time::Instant;

use wkp_core::{decode_f64, decode_geometry, encode_f64, encode_geometry, Frame};

/// Median per-call time in microseconds over `rounds` batches of ~20ms each.
fn time_us(rounds: usize, mut f: impl FnMut()) -> f64 {
    let start = Instant::now();
    let mut batch = 0;
    while start.elapsed().as_millis() < 20 {
        f();
        batch += 1;
    }
    let mut samples: Vec<f64> = (0..rounds)
        .map(|_| {
            let t = Instant::now();
            for _ in 0..batch {
                f();
            }
            t.elapsed().as_secs_f64() * 1e6 / batch as f64
        })
        .collect();
    samples.sort_by(f64::total_cmp);
    samples[rounds / 2]
}

/// splitmix64, so runs are reproducible without dependencies.
struct Rng(u64);

impl Rng {
    fn range(&mut self, lo: f64, hi: f64) -> f64 {
        self.0 = self.0.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.0;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        lo + (hi - lo) * ((z ^ (z >> 31)) >> 11) as f64 / (1u64 << 53) as f64
    }
}

/// The NZ North Island coastline (WKB LineString, 2D) as flat coords.
fn nz_coastline() -> Vec<f64> {
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/nz-north-island-coastline-simp-0.001.bin");
    let wkb = std::fs::read(path).expect("read coastline");
    let n = u32::from_le_bytes(wkb[5..9].try_into().unwrap()) as usize;
    wkb[9..9 + n * 16]
        .chunks_exact(8)
        .map(|b| f64::from_le_bytes(b.try_into().unwrap()))
        .collect()
}

fn main() {
    let rounds: usize = std::env::args().nth(1).map(|s| s.parse().unwrap()).unwrap_or(15);
    let mut rng = Rng(42);
    let random2d: Vec<f64> = (0..200_000)
        .flat_map(|_| [rng.range(-180.0, 180.0), rng.range(-90.0, 90.0)])
        .collect();
    let random3d: Vec<f64> = (0..100_000)
        .flat_map(|_| [rng.range(-180.0, 180.0), rng.range(-90.0, 90.0), rng.range(0.0, 5000.0)])
        .collect();
    let nz = nz_coastline();
    let cases: [(&str, &[f64], usize, i32); 4] = [
        ("nz-coast 12.8k pts 2D p5", &nz, 2, 5),
        ("nz-coast 12.8k pts 2D p7", &nz, 2, 7),
        ("random 200k pts 2D p5", &random2d, 2, 5),
        ("random 100k pts 3D p3", &random3d, 3, 3),
    ];

    let (mut out, mut values, mut frame) = (Vec::new(), Vec::new(), Frame::default());
    println!(
        "{:<26} {:>9} {:>9} {:>9} {:>9} {:>11}",
        "case", "enc_f64", "dec_f64", "enc_geom", "dec_geom", "bytes/value"
    );
    for (name, coords, dims, p) in cases {
        let segments = [coords.len() / dims];
        let mut enc_f = Vec::new();
        let mut enc_g = Vec::new();
        encode_f64(coords, dims, &[p], &mut enc_f).unwrap();
        encode_geometry(2, coords, dims, p, &[1], &segments, &mut enc_g).unwrap();
        let t = [
            time_us(rounds, || encode_f64(black_box(coords), dims, &[p], &mut out).unwrap()),
            time_us(rounds, || {
                decode_f64(black_box(&enc_f), dims, &[p], &mut values).unwrap()
            }),
            time_us(rounds, || {
                encode_geometry(2, black_box(coords), dims, p, &[1], &segments, &mut out).unwrap()
            }),
            time_us(rounds, || decode_geometry(black_box(&enc_g), &mut frame).unwrap()),
        ];
        println!(
            "{name:<26} {:>7.1}us {:>7.1}us {:>7.1}us {:>7.1}us {:>11.2}",
            t[0],
            t[1],
            t[2],
            t[3],
            enc_g.len() as f64 / coords.len() as f64
        );
    }
}
