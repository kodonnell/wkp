export type Position = number[];

export interface PointGeometry {
    type: 'Point';
    coordinates: Position;
}

export interface LineStringGeometry {
    type: 'LineString';
    coordinates: Position[];
}

export interface PolygonGeometry {
    type: 'Polygon';
    coordinates: Position[][];
}

export interface MultiPointGeometry {
    type: 'MultiPoint';
    coordinates: Position[];
}

export interface MultiLineStringGeometry {
    type: 'MultiLineString';
    coordinates: Position[][];
}

export interface MultiPolygonGeometry {
    type: 'MultiPolygon';
    coordinates: Position[][][];
}

export type Geometry =
    | PointGeometry
    | LineStringGeometry
    | PolygonGeometry
    | MultiPointGeometry
    | MultiLineStringGeometry
    | MultiPolygonGeometry;

export interface DecodedGeometry {
    version: number;
    precision: number;
    dimensions: number;
    geometry: Geometry;
}

export declare class Context { }

export declare const GeometryType: {
    readonly POINT: 1;
    readonly LINESTRING: 2;
    readonly POLYGON: 3;
    readonly MULTIPOINT: 4;
    readonly MULTILINESTRING: 5;
    readonly MULTIPOLYGON: 6;
};

/** @deprecated Renamed GeometryType in 0.7. */
export declare const EncodedGeometryType: typeof GeometryType;

/**
 * An input the WKP core rejects. `code` is `'INVALID_ARGUMENT'` (an argument is out of range or
 * inconsistent) or `'MALFORMED_INPUT'` (the encoded data is not valid WKP). Arguments of the
 * wrong type throw `TypeError` instead.
 */
export declare class WkpError extends Error {
    constructor(message: string, code: 'INVALID_ARGUMENT' | 'MALFORMED_INPUT');
    code: 'INVALID_ARGUMENT' | 'MALFORMED_INPUT';
}

/** Encoded WKP: an ASCII string or its bytes (a Uint8Array, including a Node Buffer). */
export type Encoded = string | Uint8Array;

export interface GeometryFrameInit {
    version: number;
    precision: number;
    dimensions: number;
    geometryType: number;
    coords: Float64Array | ArrayLike<number>;
    segmentPointCounts: Uint32Array | ArrayLike<number>;
    groupSegmentCounts: Uint32Array | ArrayLike<number>;
}

/** Flat form of a geometry: interleaved coordinates plus topology. */
export declare class GeometryFrame {
    constructor(init: GeometryFrameInit);
    version: number;
    precision: number;
    dimensions: number;
    geometryType: number;
    coords: Float64Array;
    segmentPointCounts: Uint32Array;
    groupSegmentCounts: Uint32Array;
    toGeometry(): Geometry;
    /** Cross-language binary form (40-byte header); transferable between workers. */
    toBuffer(): ArrayBuffer;
    static fromBuffer(buffer: ArrayBuffer | ArrayBufferView): GeometryFrame;
}

/** Encoded output is an ASCII string. */
export function encode(geometry: Geometry, precision: number, ctx?: Context): string;
export function decode(encoded: Encoded, ctx?: Context): DecodedGeometry;
/** The four header fields of an encoded geometry. */
export interface Header {
    version: number;
    precision: number;
    dimensions: number;
    geometryType: number;
}

export function decodeHeader(encoded: Encoded): Header;
export function decodeFrame(encoded: Encoded, ctx?: Context): GeometryFrame;
export function encodeFrame(frame: GeometryFrame, ctx?: Context): string;
export function encodeFloats(floats: number[][], precisions: number | number[], ctx?: Context): string;
export function decodeFloats(encoded: Encoded, precisions: number | number[], ctx?: Context): number[][];
/** Flat interleaved values: [x0, y0, x1, y1, ...]. */
export function decodeFloatsArray(encoded: Encoded, precisions: number | number[], ctx?: Context): Float64Array;
export function runSelfTest(): boolean;
export function coreVersion(): string;
export const bindingVersion: string;
export const coreCompatibility: string;
