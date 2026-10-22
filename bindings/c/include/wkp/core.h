#pragma once

/*
 * WKP C ABI. Implemented in Rust (bindings/c, crate `wkp-capi`) and shipped as
 * libwkp.{so,dylib,a} / wkp.{dll,lib}.
 *
 * Conventions:
 * - Every function that can fail returns a wkp_status; outputs are only written on
 *   WKP_STATUS_OK.
 * - Input pointers may be NULL when their count is 0.
 * - Results returned through a context (encoded bytes, decoded values, frames) point into that
 *   context's buffers. They stay valid until the next call using the same context, whether or
 *   not that call succeeds, or until wkp_context_free. Copy them if you need them longer.
 * - A context is not thread-safe: use one per thread (or synchronise). Functions that take no
 *   context are thread-safe.
 * - Limits: precision 0..63, with every |value| x 10^precision below 2^58; 1..16 dimensions;
 *   finite values. Geometries must have the structure SPEC.md requires of their type (one point
 *   for a point, at least two per linestring, no empty segments or groups).
 * - Running out of memory aborts the process, as Rust's allocator does.
 */

#include <stddef.h>
#include <stdint.h>

#include "_version.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /* A fixed-size int rather than an enum, whose size the C standard leaves open. */
    typedef int32_t wkp_status;

#define WKP_STATUS_OK 0
/* Bad arguments: see the limits above, or inconsistent counts. */
#define WKP_STATUS_INVALID_ARGUMENT 1
/* The encoded input is not valid WKP (including an unknown geometry type). */
#define WKP_STATUS_MALFORMED_INPUT 2
/* An internal error; please report it. */
#define WKP_STATUS_INTERNAL_ERROR 255

    typedef enum wkp_geometry_type
    {
        WKP_GEOMETRY_POINT = 1,
        WKP_GEOMETRY_LINESTRING = 2,
        WKP_GEOMETRY_POLYGON = 3,
        WKP_GEOMETRY_MULTIPOINT = 4,
        WKP_GEOMETRY_MULTILINESTRING = 5,
        WKP_GEOMETRY_MULTIPOLYGON = 6
    } wkp_geometry_type;

    /* A decoded geometry. Groups are polygons/parts and segments are rings/linestrings/points:
       segment_point_counts gives the points in each segment, group_segment_counts the segments
       in each group. coords is point-major and interleaved: [x0, y0, x1, y1, ...]. */
    typedef struct wkp_geometry_frame_f64
    {
        int version;
        int precision;
        int dimensions;
        int geometry_type; /* a wkp_geometry_type */
        const double *coords;
        size_t coord_value_count;
        const size_t *segment_point_counts;
        size_t segment_count;
        const size_t *group_segment_counts;
        size_t group_count;
    } wkp_geometry_frame_f64;

    /* Reusable buffers for encoding and decoding. Opaque: create with wkp_context_new. */
    typedef struct wkp_context wkp_context;

    /* NUL-terminated library version, for example "0.7.0" (WKP_CORE_VERSION at build time). */
    const char *wkp_version(void);

    /* A new context. Never NULL. */
    wkp_context *wkp_context_new(void);

    /* Frees a context and its buffers. NULL is ignored. */
    void wkp_context_free(wkp_context *ctx);

    /* Encodes a headerless float stream. precisions has 1 entry (all dimensions) or
       `dimensions` entries; value_count must be a multiple of dimensions. */
    wkp_status wkp_encode_f64(
        wkp_context *ctx,
        const double *values,
        size_t value_count,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const uint8_t **out_data,
        size_t *out_size);

    /* Decodes a headerless float stream encoded with the same dimensions and precisions.
       out_size receives the number of values (not bytes). */
    wkp_status wkp_decode_f64(
        wkp_context *ctx,
        const uint8_t *encoded,
        size_t encoded_size,
        size_t dimensions,
        const int *precisions,
        size_t precision_count,
        const double **out_values,
        size_t *out_size);

    /* Encodes a geometry with a header (see wkp_geometry_frame_f64 for the topology). */
    wkp_status wkp_encode_geometry_frame(
        wkp_context *ctx,
        int geometry_type,
        const double *coords,
        size_t coord_value_count,
        size_t dimensions,
        int precision,
        const size_t *segment_point_counts,
        size_t segment_count,
        const size_t *group_segment_counts,
        size_t group_count,
        const uint8_t **out_data,
        size_t *out_size);

    /* Decodes a geometry; *out_frame points into the context. */
    wkp_status wkp_decode_geometry_frame(
        wkp_context *ctx,
        const uint8_t *encoded,
        size_t encoded_size,
        const wkp_geometry_frame_f64 **out_frame);

    /* Decodes only the 4-char header. */
    wkp_status wkp_decode_geometry_header(
        const uint8_t *encoded,
        size_t encoded_size,
        int *out_version,
        int *out_precision,
        int *out_dimensions,
        int *out_geometry_type);

#ifdef __cplusplus
}
#endif
