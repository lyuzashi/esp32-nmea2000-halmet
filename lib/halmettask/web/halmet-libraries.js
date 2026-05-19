/**
 * Halmet Dashboard - Library Loader
 * 
 * ESM modules bundled into firmware for offline support.
 * Must be loaded first before other dashboard files.
 */

// === Local embedded ESM bundles ===
// halmet_preact.js: preact + hooks + htm (bundled together)
// halmet_canboat_part_XX.js: canboat parser + formatter split into byte parts
const HD_PREACT_BUNDLE_URL = '/halmet_preact.js';
const HD_CANBOAT_PART_COUNT = 8;
const HD_CANBOAT_PART_PREFIX = '/halmet_canboat_part_';
const HD_CANBOAT_PART_RETRIES = 1;
const HD_CANBOAT_PART_RETRY_DELAY_MS = 150;
const HD_CANBOAT_STATUS_EVENT = 'halmet:canboat-status';

let hdCanboatPromise = null;

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

function hdCanboatPartUrl(index) {
    return `${HD_CANBOAT_PART_PREFIX}${String(index).padStart(2, '0')}.js`;
}

function hdEmitCanboatStatus(detail) {
    window.dispatchEvent(new CustomEvent(HD_CANBOAT_STATUS_EVENT, { detail }));
}

function hdSleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

async function hdFetchPartText(url, index, total) {
    let lastErr = null;
    for (let attempt = 0; attempt <= HD_CANBOAT_PART_RETRIES; attempt++) {
        try {
            if (attempt > 0) {
                console.warn(`Halmet: retry part ${index}/${total} (attempt ${attempt + 1})`);
                hdEmitCanboatStatus({ state: 'loading', current: index, total, retry: attempt });
            }
            const res = await fetch(url);
            if (!res.ok) {
                throw new Error(`HTTP ${res.status}`);
            }
            const text = await res.text();
            console.log(`Halmet: loaded canboat part ${index}/${total}`);
            hdEmitCanboatStatus({ state: 'loading', current: index, total, retry: attempt });
            return text;
        } catch (err) {
            lastErr = err;
            if (attempt < HD_CANBOAT_PART_RETRIES) {
                await hdSleep(HD_CANBOAT_PART_RETRY_DELAY_MS);
            }
        }
    }
    throw new Error(`Failed to fetch canboat part ${index}: ${lastErr}`);
}

/**
 * Load heavy canboat module only when needed by fetching parts sequentially.
 */
function hdLoadCanboat() {
    if (!hdCanboatPromise) {
        hdCanboatPromise = (async () => {
            console.log('Halmet: loading canboat parts...');
            hdEmitCanboatStatus({ state: 'loading', current: 0, total: HD_CANBOAT_PART_COUNT, retry: 0 });
            const parts = [];
            for (let i = 1; i <= HD_CANBOAT_PART_COUNT; i++) {
                const url = hdCanboatPartUrl(i);
                parts.push(await hdFetchPartText(url, i, HD_CANBOAT_PART_COUNT));
            }

            const source = parts.join('');
            const blob = new Blob([source], { type: 'text/javascript' });
            const blobUrl = URL.createObjectURL(blob);
            try {
                const mod = await import(blobUrl);
                console.log('Halmet: canboat module imported');
                hdEmitCanboatStatus({ state: 'ready', current: HD_CANBOAT_PART_COUNT, total: HD_CANBOAT_PART_COUNT, retry: 0 });
                return mod;
            } finally {
                URL.revokeObjectURL(blobUrl);
            }
        })().catch((err) => {
            // Allow a fresh retry path after a failed load attempt.
            hdCanboatPromise = null;
            hdEmitCanboatStatus({ state: 'error', current: 0, total: HD_CANBOAT_PART_COUNT, retry: 0, error: String(err) });
            throw err;
        });
    }
    return hdCanboatPromise;
}
