/**
 * Halmet Dashboard - Core
 * 
 * WebSocket management and Preact setup.
 * Requires halmet-libraries.js to be loaded first.
 */

// Preact globals (populated after hdLoadDependencies)
let html, h, render, useState, useEffect, useRef, useMemo, useCallback;

// Canboat parser (populated after hdLoadDependencies)
let FromPgn = null;
let pgnToActisenseSerialFormat = null;

// WebSocket state
let hdWsConnection = null;
let hdWsReconnectTimer = null;
let hdMessageHandlers = [];  // Array of handlers for multi-tab support
let hdStatusHandlers = [];   // Array of status handlers
const HD_WS_RECONNECT_DELAY = 3000;

/**
 * Load bundled dependencies via ESM
 */
async function hdLoadDependencies() {
    if (html) return; // Already loaded
    
    // Load both bundles in parallel
    const [preactBundle, canboatBundle] = await Promise.all([
        hdLoadModule(HD_PREACT_BUNDLE_URL),
        hdLoadModule(HD_CANBOAT_BUNDLE_URL)
    ]);
    
    // Assign Preact globals from bundle
    h = preactBundle.h;
    render = preactBundle.render;
    html = preactBundle.html;
    useState = preactBundle.useState;
    useEffect = preactBundle.useEffect;
    useRef = preactBundle.useRef;
    useMemo = preactBundle.useMemo;
    useCallback = preactBundle.useCallback;
    
    // Assign canboat parser
    FromPgn = canboatBundle.FromPgn;
    pgnToActisenseSerialFormat = canboatBundle.pgnToActisenseSerialFormat;
    
    // Expose on window for other scripts
    window.FromPgn = FromPgn;
    window.pgnToActisenseSerialFormat = pgnToActisenseSerialFormat;
    
    console.log('Halmet: Dependencies loaded', { FromPgn: !!FromPgn, pgnToActisenseSerialFormat: !!pgnToActisenseSerialFormat });
}

/**
 * Connect to WebSocket (or add handlers to existing connection)
 * Supports multiple handlers for multi-tab scenarios.
 */
function hdConnectWebSocket(onMessage, onStatus) {
    // Add handlers to arrays (avoiding duplicates)
    if (onMessage && !hdMessageHandlers.includes(onMessage)) {
        hdMessageHandlers.push(onMessage);
    }
    if (onStatus && !hdStatusHandlers.includes(onStatus)) {
        hdStatusHandlers.push(onStatus);
    }
    
    // If already connected, just notify status and return
    if (hdWsConnection && hdWsConnection.readyState === WebSocket.OPEN) {
        if (onStatus) onStatus('connected');
        return hdWsConnection;
    }
    
    // If already connecting, just wait
    if (hdWsConnection && hdWsConnection.readyState === WebSocket.CONNECTING) {
        return hdWsConnection;
    }

    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    hdStatusHandlers.forEach(h => h('connecting'));
    
    const ws = new WebSocket(wsUrl);
    
    ws.onopen = () => {
        console.log('Dashboard WS connected');
        hdStatusHandlers.forEach(h => h('connected'));
        if (hdWsReconnectTimer) {
            clearTimeout(hdWsReconnectTimer);
            hdWsReconnectTimer = null;
        }
    };
    
    ws.onmessage = (event) => {
        hdMessageHandlers.forEach(h => h(event.data));
    };
    
    ws.onerror = (err) => {
        console.error('Dashboard WS error:', err);
        hdStatusHandlers.forEach(h => h('error'));
    };
    
    ws.onclose = () => {
        console.log('Dashboard WS closed');
        hdStatusHandlers.forEach(h => h('disconnected'));
        hdWsConnection = null;
        
        if (!hdWsReconnectTimer) {
            hdWsReconnectTimer = setTimeout(() => {
                hdWsReconnectTimer = null;
                // Reconnect with existing handlers
                if (hdMessageHandlers.length > 0 || hdStatusHandlers.length > 0) {
                    hdConnectWebSocket(null, null);
                }
            }, HD_WS_RECONNECT_DELAY);
        }
    };
    
    hdWsConnection = ws;
    return ws;
}

/**
 * Send message via WebSocket
 */
function hdSendMessage(data) {
    if (hdWsConnection && hdWsConnection.readyState === WebSocket.OPEN) {
        hdWsConnection.send(data);
        return true;
    }
    return false;
}
