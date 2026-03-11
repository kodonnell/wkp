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

export declare const EncodedGeometryType: {
    readonly POINT: 1;
    readonly LINESTRING: 2;
    readonly POLYGON: 3;
    readonly MULTIPOINT: 4;
    readonly MULTILINESTRING: 5;
    readonly MULTIPOLYGON: 6;
};

export function decodeHeader(encoded: string | Buffer | Uint8Array): [number, number, number, number];
export function decode(ctx: Context, encoded: string | Buffer | Uint8Array): DecodedGeometry;
export function encode(ctx: Context, geometry: Geometry, precision: number): string;
export function encodeFloats(ctx: Context, floats: number[][], precisions: number | number[]): Buffer;
export function decodeFloats(ctx: Context, encoded: string | Buffer | Uint8Array, precisions: number | number[]): number[][];
export function coreVersion(): string;
export const bindingVersion: string;
export const coreCompatibility: string;
