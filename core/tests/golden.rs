//! Byte-for-byte compatibility with the original C implementation, using random vectors it
//! generated before it was replaced (`tests/data/c_core_golden.tsv`).
//!
//! Line formats (tab-separated, lists comma-separated):
//! - `f64  <dims> <precisions> <values> <encoded> <decoded>`
//! - `geom <type> <dims> <precision> <groups> <segments> <coords> <encoded> <decoded>`
//! - `bad  <encoded> <status> <header> <segments> <groups> <coords>`: C decode of corrupted
//!   input, where status 0 means it decoded, 1 invalid argument, 2 malformed input.

use wkp::{decode_f64, decode_geometry, encode_f64, encode_geometry, Error, Frame};

fn list<T: std::str::FromStr>(s: &str) -> Vec<T>
where
    T::Err: std::fmt::Debug,
{
    if s.is_empty() {
        return Vec::new();
    }
    s.split(',').map(|x| x.parse().unwrap()).collect()
}

fn bits(v: &[f64]) -> Vec<u64> {
    v.iter().map(|x| x.to_bits()).collect()
}

#[test]
fn matches_c_core_golden_vectors() {
    let data = include_str!("data/c_core_golden.tsv");
    let (mut out, mut values, mut frame) = (Vec::new(), Vec::new(), Frame::default());
    let mut counts = [0; 3];
    for (i, line) in data.lines().enumerate() {
        let f: Vec<&str> = line.split('\t').collect();
        let ctx = format!("line {}: {line}", i + 1);
        match f[0] {
            "f64" => {
                let (dims, precisions) = (f[1].parse().unwrap(), list::<i32>(f[2]));
                encode_f64(&list::<f64>(f[3]), dims, &precisions, &mut out).unwrap();
                assert_eq!(out, f[4].as_bytes(), "{ctx}");
                decode_f64(f[4].as_bytes(), dims, &precisions, &mut values).unwrap();
                assert_eq!(bits(&values), bits(&list(f[5])), "{ctx}");
                counts[0] += 1;
            }
            "geom" => {
                let (geometry_type, dims, precision) =
                    (f[1].parse().unwrap(), f[2].parse().unwrap(), f[3].parse().unwrap());
                let (groups, segments) = (list::<usize>(f[4]), list::<usize>(f[5]));
                encode_geometry(
                    geometry_type,
                    &list::<f64>(f[6]),
                    dims,
                    precision,
                    &groups,
                    &segments,
                    &mut out,
                )
                .unwrap();
                assert_eq!(out, f[7].as_bytes(), "{ctx}");
                decode_geometry(f[7].as_bytes(), &mut frame).unwrap();
                assert_eq!(bits(&frame.coords), bits(&list(f[8])), "{ctx}");
                assert_eq!(
                    (frame.group_segment_counts.clone(), frame.segment_point_counts.clone()),
                    (groups, segments),
                    "{ctx}"
                );
                counts[1] += 1;
            }
            "bad" => {
                let got = decode_geometry(f[1].as_bytes(), &mut frame);
                match f[2] {
                    "0" => {
                        assert_eq!(got, Ok(()), "{ctx}");
                        let h = frame.header;
                        let header = vec![
                            h.version as i32,
                            h.precision as i32,
                            h.dimensions as i32,
                            h.geometry_type as i32,
                        ];
                        assert_eq!(header, list::<i32>(f[3]), "{ctx}");
                        assert_eq!(frame.segment_point_counts, list::<usize>(f[4]), "{ctx}");
                        assert_eq!(frame.group_segment_counts, list::<usize>(f[5]), "{ctx}");
                        assert_eq!(bits(&frame.coords), bits(&list(f[6])), "{ctx}");
                    }
                    "1" => assert_eq!(got, Err(Error::InvalidArgument), "{ctx}"),
                    "2" => assert_eq!(got, Err(Error::MalformedInput), "{ctx}"),
                    s => panic!("unexpected status {s} at {ctx}"),
                }
                counts[2] += 1;
            }
            kind => panic!("unknown kind {kind} at {ctx}"),
        }
    }
    assert_eq!(counts, [200, 200, 200]);
}
