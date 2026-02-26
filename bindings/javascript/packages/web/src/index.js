import initWkpCore from '../dist/wkp_core.js';

const STATUS = {
    OK: 0,
    INVALID_ARGUMENT: 1,
    MALFORMED_INPUT: 2,
    ALLOCATION_FAILED: 3,
    INTERNAL_ERROR: 255
};

const DEFAULT_ERROR_CAPACITY = 512;

function statusMessage(status) {
    switch (status) {
        case STATUS.INVALID_ARGUMENT:
            return 'Invalid argument';
        case STATUS.MALFORMED_INPUT:
            return 'Malformed input';
        case STATUS.ALLOCATION_FAILED:
            return 'Allocation failed';
        case STATUS.INTERNAL_ERROR:
            return 'Internal error';
        default:
            return `WKP error (status ${status})`;
    }
}

function normalizeValues(values) {
    if (values instanceof Float64Array) {
        return values;
    }
    if (Array.isArray(values)) {
        return Float64Array.from(values);
    }
    throw new TypeError('values must be Float64Array or number[]');
}

function normalizePrecisions(precisions) {
    if (!Array.isArray(precisions)) {
        throw new TypeError('precisions must be an array of integers');
    }
    return Int32Array.from(precisions);
}

function normalizeEncoded(encoded) {
    if (typeof encoded === 'string') {
        return new TextEncoder().encode(encoded);
    }
    if (encoded instanceof Uint8Array) {
        return encoded;
    }
    throw new TypeError('encoded must be string or Uint8Array');
}

function readCString(module, ptr) {
    if (!ptr) {
        return '';
    }
    const bytes = [];
    let offset = ptr;
    while (module.HEAPU8[offset] !== 0) {
        bytes.push(module.HEAPU8[offset]);
        offset += 1;
    }
    return new TextDecoder().decode(new Uint8Array(bytes));
}

function readPtr(module, ptrToPtr) {
    return module.HEAPU32[ptrToPtr >>> 2] >>> 0;
}

function readSize(module, ptrToSize) {
    return module.HEAPU32[ptrToSize >>> 2] >>> 0;
}

export async function createWkp(options) {
    const module = await initWkpCore(options);

    const encodeNative = module.cwrap(
        'wkp_wasm_encode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const decodeNative = module.cwrap(
        'wkp_wasm_decode_f64',
        'number',
        ['number', 'number', 'number', 'number', 'number', 'number', 'number', 'number', 'number']
    );

    const freeU8 = module.cwrap('wkp_wasm_free_u8', null, ['number', 'number']);
    const freeF64 = module.cwrap('wkp_wasm_free_f64', null, ['number', 'number']);

    function encodeF64(values, dimensions, precisions) {
        const v = normalizeValues(values);
        const p = normalizePrecisions(precisions);

        const valuesPtr = module._malloc(v.byteLength);
        const precisionsPtr = module._malloc(p.byteLength);
        const outDataPtrPtr = module._malloc(4);
        const outSizePtr = module._malloc(4);
        const errPtr = module._malloc(DEFAULT_ERROR_CAPACITY);

        try {
            module.HEAPF64.set(v, valuesPtr / 8);
            module.HEAP32.set(p, precisionsPtr / 4);
            module.HEAPU8.fill(0, errPtr, errPtr + DEFAULT_ERROR_CAPACITY);

            const status = encodeNative(
                valuesPtr,
                v.length,
                dimensions,
                precisionsPtr,
                p.length,
                outDataPtrPtr,
                outSizePtr,
                errPtr,
                DEFAULT_ERROR_CAPACITY
            );

            const outDataPtr = readPtr(module, outDataPtrPtr);
            const outSize = readSize(module, outSizePtr);

            if (status !== STATUS.OK) {
                const msg = readCString(module, errPtr) || statusMessage(status);
                throw new Error(msg);
            }

            const copy = module.HEAPU8.slice(outDataPtr, outDataPtr + outSize);
            freeU8(outDataPtr, outSize);
            return copy;
        } finally {
            module._free(valuesPtr);
            module._free(precisionsPtr);
            module._free(outDataPtrPtr);
            module._free(outSizePtr);
            module._free(errPtr);
        }
    }

    function decodeF64(encoded, dimensions, precisions) {
        const e = normalizeEncoded(encoded);
        const p = normalizePrecisions(precisions);

        const encodedPtr = module._malloc(e.byteLength);
        const precisionsPtr = module._malloc(p.byteLength);
        const outDataPtrPtr = module._malloc(4);
        const outSizePtr = module._malloc(4);
        const errPtr = module._malloc(DEFAULT_ERROR_CAPACITY);

        try {
            module.HEAPU8.set(e, encodedPtr);
            module.HEAP32.set(p, precisionsPtr / 4);
            module.HEAPU8.fill(0, errPtr, errPtr + DEFAULT_ERROR_CAPACITY);

            const status = decodeNative(
                encodedPtr,
                e.byteLength,
                dimensions,
                precisionsPtr,
                p.length,
                outDataPtrPtr,
                outSizePtr,
                errPtr,
                DEFAULT_ERROR_CAPACITY
            );

            const outDataPtr = readPtr(module, outDataPtrPtr);
            const outSize = readSize(module, outSizePtr);

            if (status !== STATUS.OK) {
                const msg = readCString(module, errPtr) || statusMessage(status);
                throw new Error(msg);
            }

            const view = module.HEAPF64.subarray(outDataPtr / 8, (outDataPtr / 8) + outSize);
            const copy = Float64Array.from(view);
            freeF64(outDataPtr, outSize);
            return copy;
        } finally {
            module._free(encodedPtr);
            module._free(precisionsPtr);
            module._free(outDataPtrPtr);
            module._free(outSizePtr);
            module._free(errPtr);
        }
    }

    return {
        encodeF64,
        decodeF64
    };
}
