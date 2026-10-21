//! Decode timing with the SIMD decoder off vs on, interleaved so CPU clock drift and other
//! load hit both sides equally.
//!
//! Usage: cargo run --release -p wkp-core --example simd_ab
use std::hint::black_box;
use std::path::Path;
use std::time::Instant;

use wkp_core::{decode_f64, decode_geometry, encode_f64, encode_geometry, set_simd_enabled, Frame};

fn once(batch: usize, f: &mut impl FnMut()) -> f64 {
    let t = Instant::now();
    for _ in 0..batch {
        f();
    }
    t.elapsed().as_secs_f64() * 1e6 / batch as f64
}

/// Median of alternating off/on batches, so clock drift hits both equally.
fn ab(mut f: impl FnMut()) -> (f64, f64) {
    let start = Instant::now();
    let mut batch = 0;
    while start.elapsed().as_millis() < 10 {
        f();
        batch += 1;
    }
    let (mut off, mut on) = (Vec::new(), Vec::new());
    for _ in 0..31 {
        set_simd_enabled(false);
        off.push(once(batch, &mut f));
        set_simd_enabled(true);
        on.push(once(batch, &mut f));
    }
    off.sort_by(f64::total_cmp);
    on.sort_by(f64::total_cmp);
    (off[15], on[15])
}

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

fn main() {
    let path = Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/nz-north-island-coastline-simp-0.001.bin");
    let wkb = std::fs::read(path).unwrap();
    let n = u32::from_le_bytes(wkb[5..9].try_into().unwrap()) as usize;
    let nz: Vec<f64> = wkb[9..9 + n * 16]
        .chunks_exact(8)
        .map(|b| f64::from_le_bytes(b.try_into().unwrap()))
        .collect();
    let mut rng = Rng(42);
    let random2d: Vec<f64> = (0..200_000)
        .flat_map(|_| [rng.range(-180.0, 180.0), rng.range(-90.0, 90.0)])
        .collect();
    let random3d: Vec<f64> = (0..100_000)
        .flat_map(|_| [rng.range(-180.0, 180.0), rng.range(-90.0, 90.0), rng.range(0.0, 5000.0)])
        .collect();
    // A dense GPS-like track: small steps at p6.
    let mut track = Vec::new();
    let (mut x, mut y) = (174.0, -41.0);
    for _ in 0..50_000 {
        x += rng.range(-2e-4, 2e-4);
        y += rng.range(-2e-4, 2e-4);
        track.extend([x, y]);
    }
    // The coastline with a smooth, elevation-like Z.
    let nz3: Vec<f64> = nz
        .chunks_exact(2)
        .enumerate()
        .flat_map(|(i, c)| [c[0], c[1], 100.0 + 50.0 * (i as f64 * 0.01).sin()])
        .collect();
    let cases: [(&str, &[f64], usize, i32); 8] = [
        ("nz-coast 2D p5", &nz, 2, 5),
        ("nz-coast 2D p7", &nz, 2, 7),
        ("gps track 2D p6", &track, 2, 6),
        ("nz-coast 3D p5", &nz3, 3, 5),
        ("nz-coast 3D p6", &nz3, 3, 6),
        ("random 2D p5", &random2d, 2, 5),
        ("random 3D p3", &random3d, 3, 3),
        ("random 2D p10", &random2d, 2, 10),
    ];
    println!(
        "{:<18} {:>6} {:>22} {:>22}",
        "case", "B/val", "decode_f64 off->on", "decode_geom off->on"
    );
    let (mut values, mut frame) = (Vec::new(), Frame::default());
    for (name, coords, dims, p) in cases {
        let mut enc = Vec::new();
        encode_f64(coords, dims, &[p], &mut enc).unwrap();
        let mut geom = Vec::new();
        encode_geometry(2, coords, dims, p, &[1], &[coords.len() / dims], &mut geom).unwrap();
        let (a0, a1) = ab(|| decode_f64(black_box(&enc), dims, &[p], &mut values).unwrap());
        let (b0, b1) = ab(|| decode_geometry(black_box(&geom), &mut frame).unwrap());
        println!(
            "{name:<18} {:>6.2} {a0:>7.1} -> {a1:>6.1}us {:>4.2}x {b0:>7.1} -> {b1:>6.1}us {:>4.2}x",
            enc.len() as f64 / coords.len() as f64,
            a0 / a1,
            b0 / b1
        );
    }
}
