/**
 * Halmet Dashboard - Library Loader
 * 
 * ESM modules bundled into firmware for offline support.
 * Must be loaded first before other dashboard files.
 */

// === Local embedded ESM bundles ===
// halmet_preact.js: preact + hooks + htm (bundled together)
// halmet_canboat.js: @canboat/canboatjs (includes ts-pgns)
const HD_PREACT_BUNDLE_URL = '/halmet_preact.js';
const HD_CANBOAT_BUNDLE_URL = '/halmet_canboat.js';

/**
 * Load ESM module from local embedded file
 */
async function hdLoadModule(url) {
    try {
        return await import(url);
    } catch (e) {
        console.error('Halmet: Failed to load module', url, e);
        throw e;
    }
}
