const FALLBACK_BINDING_VERSION = '0.0.1';

let missingVersionModuleWarned = false;

function isMissingGeneratedVersionModule(error) {
    return (
        error &&
        (error.code === 'ERR_MODULE_NOT_FOUND' || error.code === 'MODULE_NOT_FOUND') &&
        typeof error.message === 'string' &&
        error.message.includes('version.generated.js')
    );
}

function warnMissingVersionModule() {
    if (missingVersionModuleWarned || typeof console?.warn !== 'function') {
        return;
    }

    missingVersionModuleWarned = true;
    console.warn(
        '@wkpjs/web version.generated.js was not found; using fallback binding version 0.0.1. '
        + 'Run the build, test, or pack script to regenerate it.'
    );
}

export async function loadVersionMetadata() {
    try {
        const generated = await import('./version.generated.js');
        return {
            bindingVersion: generated.BINDING_VERSION,
            coreCompatibility: generated.CORE_COMPATIBILITY
        };
    } catch (error) {
        if (!isMissingGeneratedVersionModule(error)) {
            throw error;
        }

        warnMissingVersionModule();
        return {
            bindingVersion: FALLBACK_BINDING_VERSION,
            coreCompatibility: null
        };
    }
}