/**
 * Halmet Switch Panel
 * 
 * Visual display of PGN 127501 (Binary Status Report) and 
 * toggle controls for PGN 127502 (Binary Switch Control).
 * 
 * Uses shared WebSocket connection from halmet-dashboard-core.js
 */

(function() {
    const api = window.esp32nmea2k;
    if (!api) return;

    // PGN definitions
    const PGN_BINARY_STATUS = 127501;
    const PGN_SWITCH_CONTROL = 127502;
    
    // OFF_ON enum values
    const STATUS_OFF = 0;
    const STATUS_ON = 1;
    const STATUS_ERROR = 2;
    const STATUS_UNAVAILABLE = 3;
    const STATUS_UNKNOWN = 4;  // Not yet received from network
    
    // OFF_ON_CONTROL enum values
    const CONTROL_OFF = 0;
    const CONTROL_ON = 1;
    const CONTROL_NO_CHANGE = 3;
    
    // Number of switches to display (first 16)
    const NUM_SWITCHES = 16;
    
    // Default banks to show (can be configured)
    const DEFAULT_BANKS = [0];
    
    // State: bank -> [status1, status2, ...]
    let bankStatus = {};
    
    /**
     * Initialize default banks with unknown status
     */
    function initDefaultBanks() {
        for (const bank of DEFAULT_BANKS) {
            if (!bankStatus[bank]) {
                bankStatus[bank] = new Array(NUM_SWITCHES).fill(STATUS_UNKNOWN);
            }
        }
    }
    let wsConnected = false;
    let container = null;
    let parser = null;  // Cached FromPgn parser

    /**
     * Parse PGN 127501 fields into an array of status values
     */
    function parseStatusFields(fields) {
        const status = [];
        for (let i = 1; i <= NUM_SWITCHES; i++) {
            const key = `indicator${i}`;  // canboat uses lowercase
            const val = fields[key];
            
            if (val === undefined) {
                // Field not present - treat as unavailable
                status.push(STATUS_UNAVAILABLE);
            } else if (typeof val === 'string') {
                // canboat returns string like "On", "Off", "Unavailable", etc.
                const lower = val.toLowerCase();
                if (lower === 'on') status.push(STATUS_ON);
                else if (lower === 'off') status.push(STATUS_OFF);
                else if (lower === 'error') status.push(STATUS_ERROR);
                else status.push(STATUS_UNAVAILABLE);
            } else {
                // Numeric value (0=Off, 1=On, 2=Error, 3=Unavailable)
                status.push(val);
            }
        }
        return status;
    }

    /**
     * Build PGN 127502 message to toggle a single switch
     */
    function buildSwitchControlMessage(bankInstance, switchIndex, newState) {
        // Build fields object - all switches start as "no change"
        // canboat uses lowercase field names: instance, switch1, switch2, etc.
        const fields = { instance: bankInstance };
        for (let i = 1; i <= 28; i++) {
            fields[`switch${i}`] = CONTROL_NO_CHANGE;
        }
        // Set the target switch
        fields[`switch${switchIndex + 1}`] = newState ? CONTROL_ON : CONTROL_OFF;
        
        return {
            pgn: PGN_SWITCH_CONTROL,
            prio: 3,
            src: 0,  // Will be overwritten by gateway
            dst: 255,  // Broadcast
            fields: fields
        };
    }

    /**
     * Send switch control via WebSocket
     */
    async function sendSwitchControl(bankInstance, switchIndex, newState) {
        if (!window.pgnToActisenseSerialFormat && typeof hdEnsureCanboat === 'function') {
            try {
                await hdEnsureCanboat();
            } catch (e) {
                console.error('SwitchPanel: failed to load canboat formatter', e);
            }
        }
        if (!window.pgnToActisenseSerialFormat) {
            console.error('SwitchPanel: pgnToActisenseSerialFormat not loaded');
            return false;
        }
        
        const msg = buildSwitchControlMessage(bankInstance, switchIndex, newState);
        const actisenseStr = window.pgnToActisenseSerialFormat(msg);
        
        if (actisenseStr && hdSendMessage) {
            console.log('SwitchPanel: Sending', actisenseStr);
            return hdSendMessage(actisenseStr);
        }
        return false;
    }

    /**
     * Parse PGN 127501 from raw Actisense bytes (fallback when canboat doesn't decode indicators)
     * Format: timestamp,prio,pgn,src,dst,len,b0,b1,b2,b3,b4,b5,b6,b7
     * Bytes: b0=instance, b1-b7=indicators (2 bits each, 28 total)
     */
    function parseRawActisense127501(rawData) {
        const parts = rawData.split(',');
        if (parts.length < 8) return null;
        
        const pgn = parseInt(parts[2]);
        if (pgn !== PGN_BINARY_STATUS) return null;
        
        // Extract hex bytes after len field (index 6+)
        const dataStart = 6;
        const instance = parseInt(parts[dataStart], 16);
        
        // Parse indicator bytes (bytes 1-7, 2 bits per switch)
        const status = [];
        for (let byteIdx = 1; byteIdx <= 7 && (dataStart + byteIdx) < parts.length; byteIdx++) {
            const byte = parseInt(parts[dataStart + byteIdx], 16);
            if (isNaN(byte)) continue;
            
            // Each byte has 4 indicators (2 bits each)
            for (let bitPair = 0; bitPair < 4; bitPair++) {
                const val = (byte >> (bitPair * 2)) & 0x03;
                status.push(val);  // 0=Off, 1=On, 2=Error, 3=Unavailable
                if (status.length >= NUM_SWITCHES) break;
            }
            if (status.length >= NUM_SWITCHES) break;
        }
        
        return { instance, status };
    }

    /**
     * Handle incoming WebSocket message
     */
    function handleMessage(data) {
        // Create parser on first use
        if (!parser && window.FromPgn) {
            parser = new window.FromPgn();
            parser.on('error', () => {});  // Suppress error events
        }

        // Lazy-load parser if not yet present; raw parser fallback keeps panel functional.
        if (!parser && typeof hdEnsureCanboat === 'function') {
            hdEnsureCanboat().then((canboat) => {
                if (!parser && canboat && canboat.FromPgn) {
                    parser = new canboat.FromPgn();
                    parser.on('error', () => {});
                }
            }).catch(() => {});
        }
        
        try {
            // Try canboat parser first
            let pgn = null;
            if (parser) {
                pgn = parser.parseString(data);
            }
            
            if (pgn && pgn.pgn === PGN_BINARY_STATUS) {
                const instance = pgn.fields.instance;
                if (instance !== undefined && !isNaN(instance)) {
                    // Check if canboat parsed indicators or only instance
                    const hasIndicators = pgn.fields.indicator1 !== undefined;
                    
                    if (hasIndicators) {
                        // Use canboat parsed fields
                        bankStatus[instance] = parseStatusFields(pgn.fields);
                    } else {
                        // Fallback: parse raw Actisense bytes
                        const raw = parseRawActisense127501(data);
                        if (raw && raw.instance === instance) {
                            console.log('SwitchPanel: Using raw parse for bank', instance, raw.status.slice(0, 4));
                            bankStatus[instance] = raw.status;
                        } else {
                            // Last resort: all unavailable
                            bankStatus[instance] = parseStatusFields(pgn.fields);
                        }
                    }
                    updateUI();
                }
            }
        } catch (e) {
            console.error('SwitchPanel: Parse error', e);
        }
    }

    /**
     * Handle WebSocket status change
     */
    function handleStatus(status) {
        wsConnected = (status === 'connected');
        updateConnectionStatus();
    }

    /**
     * Create a single switch indicator/button component
     */
    function createSwitchElement(bankInstance, switchIndex, status) {
        const div = document.createElement('div');
        div.className = 'switch-item';
        div.dataset.bank = bankInstance;
        div.dataset.switch = switchIndex;
        
        // Status indicator
        const indicator = document.createElement('div');
        indicator.className = 'switch-indicator';
        
        if (status === STATUS_ON) {
            indicator.classList.add('on');
        } else if (status === STATUS_OFF) {
            indicator.classList.add('off');
        } else if (status === STATUS_ERROR) {
            indicator.classList.add('error');
        } else if (status === STATUS_UNAVAILABLE) {
            indicator.classList.add('unavailable');
        } else if (status === STATUS_UNKNOWN) {
            indicator.classList.add('unknown');
        }
        
        // Label
        const label = document.createElement('span');
        label.className = 'switch-label';
        label.textContent = `${switchIndex + 1}`;
        
        // Toggle buttons - show both ON and OFF for unknown, single button otherwise
        const buttonContainer = document.createElement('div');
        buttonContainer.className = 'switch-buttons';
        
        if (status === STATUS_UNKNOWN) {
            // Show both buttons when status is unknown
            const onBtn = document.createElement('button');
            onBtn.className = 'switch-button on-btn';
            onBtn.textContent = 'ON';
            onBtn.disabled = !wsConnected;
            onBtn.addEventListener('click', () => sendSwitchControl(bankInstance, switchIndex, true));
            
            const offBtn = document.createElement('button');
            offBtn.className = 'switch-button off-btn';
            offBtn.textContent = 'OFF';
            offBtn.disabled = !wsConnected;
            offBtn.addEventListener('click', () => sendSwitchControl(bankInstance, switchIndex, false));
            
            buttonContainer.appendChild(onBtn);
            buttonContainer.appendChild(offBtn);
        } else {
            // Single toggle button when status is known
            const button = document.createElement('button');
            button.className = 'switch-button';
            button.textContent = status === STATUS_ON ? 'OFF' : 'ON';
            button.disabled = !wsConnected;
            button.addEventListener('click', () => {
                const newState = status !== STATUS_ON;
                sendSwitchControl(bankInstance, switchIndex, newState);
            });
            buttonContainer.appendChild(button);
        }
        
        div.appendChild(indicator);
        div.appendChild(label);
        div.appendChild(buttonContainer);
        
        return div;
    }

    /**
     * Create a bank panel
     */
    function createBankPanel(bankInstance, statuses) {
        console.log('SwitchPanel: Creating bank panel', bankInstance, 'with', statuses.length, 'switches');
        const panel = document.createElement('div');
        panel.className = 'switch-bank';
        panel.dataset.bank = bankInstance;
        
        const header = document.createElement('div');
        header.className = 'switch-bank-header';
        header.textContent = `Bank ${bankInstance}`;
        panel.appendChild(header);
        
        const grid = document.createElement('div');
        grid.className = 'switch-grid';
        
        // Show first NUM_SWITCHES
        for (let i = 0; i < Math.min(NUM_SWITCHES, statuses.length); i++) {
            grid.appendChild(createSwitchElement(bankInstance, i, statuses[i]));
        }
        
        console.log('SwitchPanel: Grid has', grid.children.length, 'children');
        panel.appendChild(grid);
        return panel;
    }

    /**
     * Update the UI with current state
     */
    function updateUI() {
        if (!container) {
            console.log('SwitchPanel: updateUI - no container');
            return;
        }
        
        const panelContainer = container.querySelector('.switch-panels');
        if (!panelContainer) {
            console.log('SwitchPanel: updateUI - no panelContainer');
            return;
        }
        
        // Clear existing panels
        panelContainer.innerHTML = '';
        console.log('SwitchPanel: Updating UI with', Object.keys(bankStatus).length, 'banks');
        
        // Create panel for each bank
        const banks = Object.keys(bankStatus).sort((a, b) => a - b);
        
        for (const bank of banks) {
            panelContainer.appendChild(createBankPanel(parseInt(bank), bankStatus[bank]));
        }
    }

    /**
     * Update connection status indicator
     */
    function updateConnectionStatus() {
        if (!container) return;
        
        const statusEl = container.querySelector('.ws-status');
        if (statusEl) {
            statusEl.className = 'ws-status ' + (wsConnected ? 'connected' : 'disconnected');
            statusEl.textContent = wsConnected ? 'Connected' : 'Disconnected';
        }
        
        // Update button states
        container.querySelectorAll('.switch-button').forEach(btn => {
            btn.disabled = !wsConnected;
        });
    }

    /**
     * Initialize the switch panel
     */
    async function initSwitchPanel(containerEl) {
        container = containerEl;
        
        // Wait for dependencies
        if (typeof hdLoadDependencies === 'function') {
            await hdLoadDependencies();
        }
        
        // Initialize default banks with unknown status
        initDefaultBanks();
        
        // Build initial UI
        container.innerHTML = `
            <div class="switch-panel-header">
                <h2>Digital Switching</h2>
                <div class="ws-status disconnected">Disconnected</div>
            </div>
            <div class="switch-panels"></div>
            <div class="switch-panel-footer">
                <p>PGN 127501 (Status) / PGN 127502 (Control) - Bank 0</p>
            </div>
        `;
        
        // Render initial state
        updateUI();
        
        // Connect WebSocket
        if (typeof hdConnectWebSocket === 'function') {
            hdConnectWebSocket(handleMessage, handleStatus);
        }
    }

    /**
     * Register tab on init
     */
    api.registerListener((id, data) => {
        // Create a new tab page
        const switchPage = api.addTabPage('switchPanel', 'Switches');
        if (!switchPage) {
            console.error('SwitchPanel: Failed to create tab page');
            return;
        }
        
        const panelContainer = document.createElement('div');
        panelContainer.id = 'halmet-switch-panel';
        switchPage.appendChild(panelContainer);

        let initialized = false;
        
        // Initialize when tab is clicked
        api.registerListener((tabId, tabData) => {
            if (tabData === 'switchPanel' && !initialized) {
                initialized = true;
                initSwitchPanel(panelContainer);
            }
        }, api.EVENTS.tab);

    }, api.EVENTS.init);
})();
