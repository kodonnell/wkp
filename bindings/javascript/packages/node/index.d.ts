export function encodeF64(values: readonly number[] | Float64Array, dimensions: number, precisions: readonly number[]): Buffer;
export function decodeF64(encoded: string | Buffer | Uint8Array, dimensions: number, precisions: readonly number[]): Float64Array;
