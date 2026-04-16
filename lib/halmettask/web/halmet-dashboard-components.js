/**
 * Halmet Dashboard - Components
 * 
 * Preact components for the dashboard.
 */

let hdParser = null;

// Conversion functions
const kelvinToCelsius = { convert: v => v - 273.15, from: 'K', to: '°C', decimals: 1 };
const msToKnots = { convert: v => v * 1.94384, from: 'm/s', to: 'kn', decimals: 1 };
const radToDeg = { convert: v => v * 180 / Math.PI, from: 'rad', to: '°', decimals: 1 };
const radsToDegS = { convert: v => v * 180 / Math.PI, from: 'rad/s', to: '°/s', decimals: 1 };
const paToHpa = { convert: v => v / 100, from: 'Pa', to: 'hPa', decimals: 1 };
const paToBar = { convert: v => v / 100000, from: 'Pa', to: 'bar', decimals: 2 };
const meters = { convert: v => v, from: 'm', to: 'm', decimals: 1 };

// Unit conversion map: field name (from canboatjs v3, camelCase) → conversion
const hdConversions = {
    // Temperature: K → °C
    'temperature': kelvinToCelsius,
    'actualTemperature': kelvinToCelsius,
    'airTemperature': kelvinToCelsius,
    'coolantTemperature': kelvinToCelsius,
    'oilTemperature': kelvinToCelsius,
    'outsideAmbientAirTemperature': kelvinToCelsius,
    'waterTemperature': kelvinToCelsius,
    'motorTemperature': kelvinToCelsius,
    'dewpoint': kelvinToCelsius,
    'setTemperature': kelvinToCelsius,
    
    // Speed: m/s → knots
    'sog': msToKnots,
    'speedWaterReferenced': msToKnots,
    'speedGroundReferenced': msToKnots,
    'windSpeed': msToKnots,
    'windGusts': msToKnots,
    'currentSpeed': msToKnots,
    'drift': msToKnots,
    'waypointClosingVelocity': msToKnots,
    
    // Angles: rad → degrees
    'heading': radToDeg,
    'cog': radToDeg,
    'windAngle': radToDeg,
    'windDirection': radToDeg,
    'bearing': radToDeg,
    'pitch': radToDeg,
    'roll': radToDeg,
    'yaw': radToDeg,
    'deviation': radToDeg,
    'variation': radToDeg,
    'set': radToDeg,
    'track': radToDeg,
    'azimuth': radToDeg,
    'elevation': radToDeg,
    'rudderPosition': radToDeg,
    'position': radToDeg,
    'angle': radToDeg,
    
    // Angular rate: rad/s → °/s
    'rateOfTurn': radsToDegS,
    
    // Atmospheric pressure: Pa → hPa
    'atmosphericPressure': paToHpa,
    'pressure': paToHpa,
    
    // Engine/hydraulic pressure: Pa → bar
    'boostPressure': paToBar,
    'coolantPressure': paToBar,
    'fuelPressure': paToBar,
    'oilPressure': paToBar,
    
    // Depth
    'depth': meters,
};

function hdFormatValue(name, value) {
    if (typeof value !== 'number') return { display: value };
    
    const conv = hdConversions[name];
    if (!conv) {
        // No conversion, just format number
        return { display: Number.isInteger(value) ? value : value.toFixed(2) };
    }
    
    // Format: original value + unit (converted value + unit)
    const convertedValue = conv.convert(value);
    const origFormatted = Number.isInteger(value) ? value : value.toFixed(2);
    const convFormatted = convertedValue.toFixed(conv.decimals);
    return {
        display: `${origFormatted} ${conv.from} (${convFormatted} ${conv.to})`
    };
}

/**
 * Message Logger Component
 */
function MessageLogger() {
    const [messages, setMessages] = useState([]);
    const [status, setStatus] = useState('disconnected');
    const [paused, setPaused] = useState(false);
    const [filter, setFilter] = useState('');
    const pausedRef = useRef(paused);
    const maxMessages = 100;

    useEffect(() => { pausedRef.current = paused; }, [paused]);

    // Init parser once
    useEffect(() => {
        if (!hdParser && typeof FromPgn !== 'undefined') {
            hdParser = new FromPgn();
            hdParser.on('error', () => {});
        }
    }, []);

    // Connect on mount
    useEffect(() => {
        const handleMessage = (data) => {
            if (pausedRef.current || !hdParser) return;
            
            const msg = hdParser.parseString(data);
            if (!msg) return;
            
            Object.defineProperty(msg, 'raw', { value: data });
            setMessages(prev => [{ id: Date.now(), msg }, ...prev].slice(0, maxMessages));
        };
        
        hdConnectWebSocket(handleMessage, setStatus);
    }, []);

    const filteredMessages = filter 
        ? messages.filter(m => {
            const s = filter.toLowerCase();
            return String(m.msg.pgn).includes(s) || m.msg.description?.toLowerCase().includes(s);
        })
        : messages;

    const statusColor = { connected: '#4caf50', connecting: '#ff9800', disconnected: '#9e9e9e', error: '#f44336' }[status] || '#9e9e9e';

    return html`
        <div class="halmet-dashboard">
            <div class="dashboard-header">
                <h3>N2K Stream</h3>
                <div class="dashboard-controls">
                    <span class="status-indicator" style="background: ${statusColor}"></span>
                    <span class="status-text">${status}</span>
                    <input type="text" placeholder="Filter..." value=${filter} onInput=${e => setFilter(e.target.value)} class="filter-input" />
                    <button onClick=${() => setPaused(!paused)} class="control-btn ${paused ? 'paused' : ''}">${paused ? '▶' : '⏸'}</button>
                    <button onClick=${() => setMessages([])} class="control-btn">🗑</button>
                </div>
            </div>
            <div class="message-log">
                ${filteredMessages.map(m => html`<${MessageEntry} key=${m.id} msg=${m.msg} />`)}
            </div>
        </div>
    `;
}

function MessageEntry({ msg }) {
    const [expanded, setExpanded] = useState(false);
    const hasFields = msg.fields && Object.keys(msg.fields).length > 0;
    
    return html`
        <div class="log-entry ${expanded ? 'expanded' : ''}" onClick=${() => setExpanded(!expanded)}>
            <div class="log-entry-header">
                <span class="log-pgn">${msg.pgn}</span>
                <span class="log-name">${msg.description}</span>
                <span class="log-src">src:${msg.src}</span>
                ${hasFields && html`<span class="log-expand">${expanded ? '▼' : '▶'}</span>`}
            </div>
            ${expanded && hasFields && html`
                <div class="log-entry-details">
                <div class="log-raw">${msg.raw}</div>
                    ${Object.entries(msg.fields).map(([k, v]) => {
                        const fmt = hdFormatValue(k, v);
                        return html`
                            <div class="log-field">
                                <span class="field-name">${k}:</span> 
                                <span class="field-value">${fmt.display}</span>
                            </div>
                        `;
                    })}
                </div>
            `}
        </div>
    `;
}
