// Types for @wkpjs/web. Geometries, frames and headers have the same shapes as in @wkpjs/node;
// the difference is that everything comes from the object `createWkp()` resolves to.

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

/** The four header fields of an encoded geometry. */
export interface Header {
    version: number;
    precision: number;
    dimensions: number;
    geometryType: number;
}

/** Encoded WKP: an ASCII string or its bytes. */
export type Encoded = string | Uint8Array;

export interface GeometryTypes {
    readonly POINT: 1;
    readonly LINESTRING: 2;
    readonly POLYGON: 3;
    readonly MULTIPOINT: 4;
    readonly MULTILINESTRING: 5;
    readonly MULTIPOLYGON: 6;
}

/** Reusable buffers in wasm memory. Call dispose() when done with one you created. */
export interface Context {
    dispose(): void;
}

export interface ContextConstructor {
    /** Buffers start at `initialCapacity` bytes (default 4096) and grow as needed. */
    new (initialCapacity?: number): Context;
}

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
export interface GeometryFrame {
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
}

export interface GeometryFrameConstructor {
    new (init: GeometryFrameInit): GeometryFrame;
    fromBuffer(buffer: ArrayBuffer | ArrayBufferView): GeometryFrame;
}

/**
 * An input the WKP core rejects. `code` is `'INVALID_ARGUMENT'` (an argument is out of range or
 * inconsistent) or `'MALFORMED_INPUT'` (the encoded data is not valid WKP). Arguments of the
 * wrong type throw `TypeError` instead. The same class for every `createWkp()` instance.
 */
export declare class WkpError extends Error {
    constructor(message: string, code: 'INVALID_ARGUMENT' | 'MALFORMED_INPUT');
    code: 'INVALID_ARGUMENT' | 'MALFORMED_INPUT';
}

export interface Wkp {
    /** Encoded output is an ASCII string. */
    encode(geometry: Geometry, precision: number, ctx?: Context): string;
    decode(encoded: Encoded, ctx?: Context): DecodedGeometry;
    decodeHeader(encoded: Encoded): Header;
    decodeFrame(encoded: Encoded, ctx?: Context): GeometryFrame;
    encodeFrame(frame: GeometryFrame, ctx?: Context): string;
    encodeFloats(floats: number[][], precisions: number | number[], ctx?: Context): string;
    decodeFloats(encoded: Encoded, precisions: number | number[], ctx?: Context): number[][];
    /** Flat interleaved values: [x0, y0, x1, y1, ...]. */
    decodeFloatsArray(encoded: Encoded, precisions: number | number[], ctx?: Context): Float64Array;
    runSelfTest(): boolean;
    coreVersion(): string;
    readonly bindingVersion: string;
    readonly coreCompatibility: string;
    readonly Context: ContextConstructor;
    readonly GeometryFrame: GeometryFrameConstructor;
    readonly GeometryType: GeometryTypes;
    readonly WkpError: typeof WkpError;
    /** @deprecated Renamed GeometryType in 0.7. */
    readonly EncodedGeometryType: GeometryTypes;
}

export interface CreateWkpOptions {
    /** A compiled module to instantiate. */
    wasmModule?: WebAssembly.Module;
    /** The module's bytes. */
    wasmBinary?: ArrayBuffer | ArrayBufferView;
    /** The URL to fetch `wkp_core.wasm` from (browsers; Node reads the bundled file). */
    locateFile?: (path: string) => string | URL;
}

/** Loads the WebAssembly module and returns the API bound to it. */
export function createWkp(options?: CreateWkpOptions): Promise<Wkp>;
