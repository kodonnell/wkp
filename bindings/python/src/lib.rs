//! `wkp._core`: the native extension behind the `wkp` Python package (`python/wkp`).

use numpy::{PyArray1, PyArray2, PyArrayMethods, PyReadonlyArray2, PyUntypedArrayMethods};
use pyo3::exceptions::{PyRuntimeError, PyTypeError, PyValueError};
use pyo3::prelude::*;
use pyo3::types::{PyBytes, PyDict, PyString, PyTuple};
use wkp::Frame;

const VERSION: &str = env!("CARGO_PKG_VERSION");

/// Inputs at least this many values are processed with the GIL released. Below it, the cost
/// of releasing and re-acquiring outweighs the work.
const DETACH_MIN_VALUES: usize = 16 * 1024;

/// Reusable native buffers. Buffers grow to fit the largest geometry processed, then stay
/// allocated and are reused on subsequent calls.
#[pyclass(module = "wkp._core")]
struct Context {
    bytes: Vec<u8>,
    frame: Frame,
}

#[pymethods]
impl Context {
    #[new]
    fn new() -> Self {
        Context {
            bytes: Vec::new(),
            frame: Frame::default(),
        }
    }
}

fn core_err(e: wkp::Error) -> PyErr {
    PyValueError::new_err(format!("WKP core error: {e}"))
}

fn run<T: Send>(py: Python<'_>, values: usize, f: impl Send + FnOnce() -> T) -> T {
    if values >= DETACH_MIN_VALUES {
        py.detach(f)
    } else {
        f()
    }
}

/// The bytes of an encoded value: a `bytes` object or a `str`, both borrowed without copying.
/// A `str` is read as UTF-8, which for the usual all-ASCII string is CPython's own buffer; any
/// non-ASCII char becomes bytes >= 0x80, which the decoder rejects. The wrapper converts other
/// bytes-like objects to `bytes` first.
fn encoded_bytes<'a>(obj: &'a Bound<'_, PyAny>) -> PyResult<&'a [u8]> {
    if let Ok(b) = obj.cast::<PyBytes>() {
        return Ok(b.as_bytes());
    }
    if let Ok(s) = obj.cast::<PyString>() {
        return Ok(s.to_str()?.as_bytes());
    }
    Err(PyTypeError::new_err("encoded must be str or bytes"))
}

/// Counts from a uint64 numpy array (read directly, as the wrapper passes them) or any sequence
/// of ints (extracted one by one, which is slow for thousands of entries).
fn counts(obj: &Bound<'_, PyAny>) -> PyResult<Vec<usize>> {
    if let Ok(array) = obj.cast::<PyArray1<u64>>() {
        if let Ok(values) = array.readonly().as_slice() {
            return values
                .iter()
                .map(|&v| usize::try_from(v).map_err(|_| core_err(wkp::Error::InvalidArgument)))
                .collect();
        }
    }
    obj.extract()
}

fn contiguous<'a>(a: &'a PyReadonlyArray2<'_, f64>) -> PyResult<&'a [f64]> {
    a.as_slice()
        .map_err(|_| PyValueError::new_err("array must be C-contiguous"))
}

fn array2<'py>(py: Python<'py>, values: &[f64], dims: usize) -> Bound<'py, PyArray2<f64>> {
    let arr = unsafe { PyArray2::<f64>::new(py, [values.len() / dims, dims], false) };
    unsafe { arr.as_slice_mut().unwrap().copy_from_slice(values) };
    arr
}

fn header_tuple(h: wkp::Header) -> (i32, i32, i32, i32) {
    (
        h.version as i32,
        h.precision as i32,
        h.dimensions as i32,
        h.geometry_type as i32,
    )
}

#[pyfunction]
fn core_version() -> &'static str {
    VERSION
}

#[pyfunction]
fn run_self_test() -> PyResult<bool> {
    self_test().map(|()| true)
}

fn self_test() -> PyResult<()> {
    let values = [38.5, -120.2, 40.7, -120.95, 43.252, -126.453];
    let mut buf = Vec::new();
    let mut frame = Frame::default();
    let ok = wkp::encode_f64(&values, 2, &[5], &mut buf).is_ok()
        && buf == b"_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::encode_geometry(2, &values, 2, 5, &[1], &[3], &mut buf).is_ok()
        && buf == b"@DAA_p~iF~ps|U_ulLnnqC_mqNvxq`@"
        && wkp::decode_geometry(&buf, &mut frame).is_ok()
        && frame.coords == values;
    if ok {
        Ok(())
    } else {
        Err(PyRuntimeError::new_err("WKP core self-test failed"))
    }
}

#[pyfunction]
fn decode_geometry_header(encoded: &Bound<'_, PyAny>) -> PyResult<(i32, i32, i32, i32)> {
    let encoded = encoded_bytes(encoded)?;
    wkp::decode_header(encoded).map(header_tuple).map_err(core_err)
}

#[pyfunction]
fn encode_f64<'py>(
    py: Python<'py>,
    mut ctx: PyRefMut<'_, Context>,
    values: PyReadonlyArray2<'py, f64>,
    dimensions: usize,
    precisions: Vec<i32>,
) -> PyResult<Bound<'py, PyBytes>> {
    let values = contiguous(&values)?;
    let bytes = &mut ctx.bytes;
    run(py, values.len(), || {
        wkp::encode_f64(values, dimensions, &precisions, bytes)
    })
    .map_err(core_err)?;
    Ok(PyBytes::new(py, &ctx.bytes))
}

#[pyfunction]
fn decode_f64<'py>(
    py: Python<'py>,
    mut ctx: PyRefMut<'_, Context>,
    encoded: &Bound<'py, PyAny>,
    dimensions: usize,
    precisions: Vec<i32>,
) -> PyResult<Bound<'py, PyArray1<f64>>> {
    let encoded = encoded_bytes(encoded)?;
    let coords = &mut ctx.frame.coords;
    run(py, encoded.len(), || {
        wkp::decode_f64(encoded, dimensions, &precisions, coords)
    })
    .map_err(core_err)?;
    Ok(PyArray1::from_slice(py, &ctx.frame.coords))
}

#[pyfunction]
fn encode_geometry_frame<'py>(
    py: Python<'py>,
    mut ctx: PyRefMut<'_, Context>,
    geometry_type: i32,
    coords: PyReadonlyArray2<'py, f64>,
    precision: i32,
    group_segment_counts: &Bound<'py, PyAny>,
    segment_point_counts: &Bound<'py, PyAny>,
) -> PyResult<Bound<'py, PyBytes>> {
    let group_segment_counts = counts(group_segment_counts)?;
    let segment_point_counts = counts(segment_point_counts)?;
    let dims = coords.shape()[1];
    let coords = contiguous(&coords)?;
    let geometry_type = u8::try_from(geometry_type).map_err(|_| core_err(wkp::Error::InvalidArgument))?;
    let bytes = &mut ctx.bytes;
    run(py, coords.len(), || {
        wkp::encode_geometry(
            geometry_type,
            coords,
            dims,
            precision,
            &group_segment_counts,
            &segment_point_counts,
            bytes,
        )
    })
    .map_err(core_err)?;
    Ok(PyBytes::new(py, &ctx.bytes))
}

fn decode_into(py: Python<'_>, ctx: &mut Context, encoded: &Bound<'_, PyAny>) -> PyResult<()> {
    let encoded = encoded_bytes(encoded)?;
    let frame = &mut ctx.frame;
    run(py, encoded.len(), || wkp::decode_geometry(encoded, frame)).map_err(core_err)
}

/// Returns `(version, precision, dimensions, geometry_type, coords, segment_point_counts,
/// group_segment_counts)` with `coords` shaped `(total_points, dimensions)` and uint64 counts.
#[pyfunction]
fn decode_geometry_frame_flat<'py>(
    py: Python<'py>,
    mut ctx: PyRefMut<'_, Context>,
    encoded: &Bound<'py, PyAny>,
) -> PyResult<Bound<'py, PyTuple>> {
    decode_into(py, &mut ctx, encoded)?;
    let f = &ctx.frame;
    let coords = array2(py, &f.coords, f.header.dimensions as usize);
    let segs = PyArray1::from_iter(py, f.segment_point_counts.iter().map(|&n| n as u64));
    let groups = PyArray1::from_iter(py, f.group_segment_counts.iter().map(|&n| n as u64));
    let (v, p, d, t) = header_tuple(f.header);
    (v, p, d, t, coords, segs, groups).into_pyobject(py)
}

#[pymodule]
fn _core(m: &Bound<'_, PyModule>) -> PyResult<()> {
    let py = m.py();
    self_test()?;
    m.add("__version__", VERSION)?;
    m.add_class::<Context>()?;

    let members = PyDict::new(py);
    for (name, code) in [
        ("POINT", 1),
        ("LINESTRING", 2),
        ("POLYGON", 3),
        ("MULTIPOINT", 4),
        ("MULTILINESTRING", 5),
        ("MULTIPOLYGON", 6),
    ] {
        members.set_item(name, code)?;
    }
    let kwargs = PyDict::new(py);
    kwargs.set_item("module", "wkp._core")?;
    let enum_cls = py
        .import("enum")?
        .getattr("IntEnum")?
        .call(("EncodedGeometryType", members), Some(&kwargs))?;
    m.add("EncodedGeometryType", enum_cls)?;

    m.add_function(wrap_pyfunction!(core_version, m)?)?;
    m.add_function(wrap_pyfunction!(run_self_test, m)?)?;
    m.add_function(wrap_pyfunction!(decode_geometry_header, m)?)?;
    m.add_function(wrap_pyfunction!(encode_f64, m)?)?;
    m.add_function(wrap_pyfunction!(decode_f64, m)?)?;
    m.add_function(wrap_pyfunction!(encode_geometry_frame, m)?)?;
    m.add_function(wrap_pyfunction!(decode_geometry_frame_flat, m)?)?;
    Ok(())
}
