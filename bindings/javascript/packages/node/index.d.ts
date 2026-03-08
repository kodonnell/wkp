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

export declare class Workspace {
    constructor(initialCapacity?: number);
    readonly initialCapacity: number;
    encodeF64(values: readonly number[] | Float64Array, dimensions: number, precisions: readonly number[]): Buffer;
    decodeF64(encoded: string | Buffer | Uint8Array, dimensions: number, precisions: readonly number[]): Float64Array;
}

export function decodeHeader(encoded: string | Buffer | Uint8Array): [number, number, number, number];
export function decode(encoded: string | Buffer | Uint8Array, workspace?: Workspace): DecodedGeometry;
export function encodePoint(geometry: PointGeometry, precision: number, workspace?: Workspace): string;
export function encodeLineString(geometry: LineStringGeometry, precision: number, workspace?: Workspace): string;
export function encodePolygon(geometry: PolygonGeometry, precision: number, workspace?: Workspace): string;
export function encodeMultiPoint(geometry: MultiPointGeometry, precision: number, workspace?: Workspace): string;
export function encodeMultiLineString(geometry: MultiLineStringGeometry, precision: number, workspace?: Workspace): string;
export function encodeMultiPolygon(geometry: MultiPolygonGeometry, precision: number, workspace?: Workspace): string;
export function encodeF64(values: readonly number[] | Float64Array, dimensions: number, precisions: readonly number[], workspace?: Workspace): Buffer;
export function decodeF64(encoded: string | Buffer | Uint8Array, dimensions: number, precisions: readonly number[], workspace?: Workspace): Float64Array;
export function coreVersion(): string;
export const bindingVersion: string;
export const coreCompatibility: string;
