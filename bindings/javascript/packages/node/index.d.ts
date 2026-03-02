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

export declare const EncodedGeometryType: {
    readonly POINT: 1;
    readonly LINESTRING: 2;
    readonly POLYGON: 3;
    readonly MULTIPOINT: 4;
    readonly MULTILINESTRING: 5;
    readonly MULTIPOLYGON: 6;
};

export declare class GeometryEncoder {
    constructor(precision: number, dimensions: number, initialCapacity?: number);
    readonly precision: number;
    readonly dimensions: number;
    readonly initialCapacity: number;

    encodeBytes(geometry: Geometry): Buffer;
    encode(geometry: Geometry): string;
    encodeStr(geometry: Geometry): string;

    decodeBytes(encoded: string | Buffer | Uint8Array): DecodedGeometry;
    decodeStr(encoded: string): DecodedGeometry;

    static decodeHeader(encoded: string | Buffer | Uint8Array): [number, number, number, number];
    static decode(encoded: string | Buffer | Uint8Array): DecodedGeometry;
}

export function encodeF64(values: readonly number[] | Float64Array, dimensions: number, precisions: readonly number[]): Buffer;
export function decodeF64(encoded: string | Buffer | Uint8Array, dimensions: number, precisions: readonly number[]): Float64Array;
export function decodeGeometryHeader(encoded: string | Buffer | Uint8Array): [number, number, number, number];
export function coreVersion(): string;
export const bindingVersion: string;
export const coreCompatibility: string;
