//! Shared cross-language fixtures in `data/integration_tests` (see its README and MANIFEST).

use std::fs;
use std::path::{Path, PathBuf};

use wkp::{decode_f64, decode_geometry, encode_f64, encode_geometry, Frame, GeometryType};

fn fixture_root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../data/integration_tests")
}

/// `(file, fields)` for every case line of every `.txt` file in `category`.
fn cases(category: &str) -> Vec<(String, Vec<String>)> {
    let mut files: Vec<_> = fs::read_dir(fixture_root().join(category))
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "txt"))
        .collect();
    files.sort();
    let mut out = Vec::new();
    for path in files {
        let name = path.file_name().unwrap().to_string_lossy().into_owned();
        for line in fs::read_to_string(&path).unwrap().lines() {
            let line = line.trim();
            if !line.is_empty() && !line.starts_with('#') {
                out.push((name.clone(), line.split('\t').map(str::to_owned).collect()));
            }
        }
    }
    out
}

fn manifest_total(category: &str) -> usize {
    let manifest = fs::read_to_string(fixture_root().join("MANIFEST.json")).unwrap();
    let totals = &manifest[manifest.find("\"totals\"").unwrap()..];
    let key = format!("\"{category}\":");
    let rest = &totals[totals.find(&key).unwrap() + key.len()..];
    rest.trim_start()
        .split(|c: char| !c.is_ascii_digit())
        .next()
        .unwrap()
        .parse()
        .unwrap()
}

/// All numbers in a JSON-style nested list such as `[[1.0, 2.0], [3.0, 4.0]]`.
fn numbers(s: &str) -> Vec<f64> {
    s.split(|c: char| matches!(c, '[' | ']' | ',') || c.is_whitespace())
        .filter(|t| !t.is_empty())
        .map(|t| t.parse().unwrap())
        .collect()
}

fn ints(s: &str) -> Vec<i32> {
    numbers(s).into_iter().map(|v| v as i32).collect()
}

fn assert_close(got: &[f64], want: &[f64], ctx: &str) {
    assert_eq!(got.len(), want.len(), "{ctx}");
    for (g, w) in got.iter().zip(want) {
        assert!((g - w).abs() <= 1e-9, "{ctx}: {got:?} != {want:?}");
    }
}

/// A geometry parsed from WKT into the flat frame form used by the encoder.
#[derive(Debug, PartialEq)]
struct Wkt {
    geometry_type: GeometryType,
    dims: usize,
    coords: Vec<f64>,
    groups: Vec<usize>,
    segments: Vec<usize>,
}

enum Node {
    Coord(Vec<f64>),
    List(Vec<Node>),
}

fn parse_node(s: &mut &str) -> Node {
    *s = s.trim_start();
    if let Some(rest) = s.strip_prefix('(') {
        *s = rest;
        let mut items = Vec::new();
        loop {
            items.push(parse_node(s));
            *s = s.trim_start();
            let (c, rest) = s.split_at(1);
            *s = rest;
            match c {
                "," => continue,
                ")" => return Node::List(items),
                _ => panic!("bad WKT near {s:?}"),
            }
        }
    }
    let end = s.find([',', ')']).unwrap();
    let coord = s[..end].split_whitespace().map(|t| t.parse().unwrap()).collect();
    *s = &s[end..];
    Node::Coord(coord)
}

fn list(node: &Node) -> &[Node] {
    match node {
        Node::List(items) => items,
        Node::Coord(_) => panic!("expected list"),
    }
}

/// Appends a list of coords as one segment.
fn push_segment(node: &Node, w: &mut Wkt) {
    let items = list(node);
    for item in items {
        let Node::Coord(c) = item else {
            panic!("expected coordinates")
        };
        w.dims = c.len();
        w.coords.extend(c);
    }
    w.segments.push(items.len());
}

fn clone_node(n: &Node) -> Node {
    match n {
        Node::Coord(c) => Node::Coord(c.clone()),
        Node::List(l) => Node::List(l.iter().map(clone_node).collect()),
    }
}

fn parse_wkt(s: &str) -> Wkt {
    let s = s.trim();
    let open = s.find('(').unwrap();
    let kind = s[..open].trim().to_ascii_uppercase();
    let mut rest = &s[open..];
    let root = parse_node(&mut rest);
    let mut w = Wkt {
        geometry_type: GeometryType::Point,
        dims: 0,
        coords: Vec::new(),
        groups: Vec::new(),
        segments: Vec::new(),
    };
    match kind.as_str() {
        "POINT" | "LINESTRING" => {
            w.geometry_type = if kind == "POINT" {
                GeometryType::Point
            } else {
                GeometryType::LineString
            };
            push_segment(&root, &mut w);
            w.groups.push(1);
        }
        "POLYGON" => {
            w.geometry_type = GeometryType::Polygon;
            list(&root).iter().for_each(|ring| push_segment(ring, &mut w));
            w.groups.push(list(&root).len());
        }
        "MULTIPOINT" | "MULTILINESTRING" => {
            w.geometry_type = if kind == "MULTIPOINT" {
                GeometryType::MultiPoint
            } else {
                GeometryType::MultiLineString
            };
            for part in list(&root) {
                match part {
                    // MULTIPOINT (1 2, 3 4) form.
                    Node::Coord(_) => push_segment(&Node::List(vec![clone_node(part)]), &mut w),
                    Node::List(_) => push_segment(part, &mut w),
                }
                w.groups.push(1);
            }
        }
        "MULTIPOLYGON" => {
            w.geometry_type = GeometryType::MultiPolygon;
            for polygon in list(&root) {
                list(polygon).iter().for_each(|ring| push_segment(ring, &mut w));
                w.groups.push(list(polygon).len());
            }
        }
        _ => panic!("unsupported WKT type {kind}"),
    }
    w
}

#[test]
fn floats_encode() {
    let all = cases("floats/encode");
    assert_eq!(all.len(), manifest_total("floats/encode"));
    let mut out = Vec::new();
    for (file, f) in all {
        let precisions = ints(&f[0]);
        encode_f64(&numbers(&f[1]), precisions.len(), &precisions, &mut out).unwrap();
        assert_eq!(String::from_utf8_lossy(&out), f[2], "{file}: {f:?}");
    }
}

#[test]
fn floats_decode() {
    let all = cases("floats/decode");
    assert_eq!(all.len(), manifest_total("floats/decode"));
    let mut out = Vec::new();
    for (file, f) in all {
        let precisions = ints(&f[0]);
        decode_f64(f[1].as_bytes(), precisions.len(), &precisions, &mut out).unwrap();
        assert_close(&out, &numbers(&f[2]), &format!("{file}: {f:?}"));
    }
}

#[test]
fn geometry_encode() {
    let all = cases("geometry/encode");
    assert_eq!(all.len(), manifest_total("geometry/encode"));
    let mut out = Vec::new();
    for (file, f) in all {
        let w = parse_wkt(&f[1]);
        encode_geometry(
            w.geometry_type,
            &w.coords,
            w.dims,
            f[0].parse().unwrap(),
            &w.segments,
            &w.groups,
            &mut out,
        )
        .unwrap();
        assert_eq!(String::from_utf8_lossy(&out), f[2], "{file}: {f:?}");
    }
}

#[test]
fn geometry_decode() {
    let all = cases("geometry/decode");
    assert_eq!(all.len(), manifest_total("geometry/decode"));
    let mut frame = Frame::default();
    for (file, f) in all {
        let w = parse_wkt(&f[1]);
        let ctx = format!("{file}: {f:?}");
        decode_geometry(f[0].as_bytes(), &mut frame).unwrap();
        assert_eq!(frame.header.geometry_type, w.geometry_type, "{ctx}");
        assert_eq!(frame.header.dimensions as usize, w.dims, "{ctx}");
        assert_eq!(frame.group_segment_counts, w.groups, "{ctx}");
        assert_eq!(frame.segment_point_counts, w.segments, "{ctx}");
        assert_close(&frame.coords, &w.coords, &ctx);
    }
}

#[test]
fn wkt_parser_topology() {
    let w = parse_wkt(
        "MULTIPOLYGON (((0 0, 1 0, 1 1, 0 0)), ((5 5, 6 5, 6 6, 5 5), (5.2 5.2, 5.4 5.2, 5.2 5.4, 5.2 5.2)))",
    );
    assert_eq!(
        (w.geometry_type, w.dims, w.groups, w.segments),
        (GeometryType::MultiPolygon, 2, vec![1, 2], vec![4, 4, 4])
    );
    let w = parse_wkt("MULTIPOINT (1 2, 3 4)");
    assert_eq!(
        (w.groups, w.segments, w.coords),
        (vec![1, 1], vec![1, 1], vec![1.0, 2.0, 3.0, 4.0])
    );
}
