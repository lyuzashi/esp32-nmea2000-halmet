(function(){
    const api=window.esp32nmea2k;
    if (! api) return;
    
    // Heap chart state
    const heapHistory = [];
    const HEAP_HISTORY_MAX = 300;  // 5 minutes at 1/sec
    let heapChartCanvas = null;
    let heapMinEl = null;
    let heapMaxEl = null;
    let heapAllTimeMinEl = null;
    let heapTrendEl = null;
    let injected = false;
    
    // Long-term tracking (survives history window)
    let allTimeMin = Infinity;
    let trendSamples = [];  // Longer-term samples for trend (1 per 10 sec)
    let lastTrendSample = 0;
    const TREND_INTERVAL = 10000;  // Sample every 10 seconds
    const TREND_SAMPLES_MAX = 360;  // 1 hour of trend data
    
    function injectHeapChart() {
        if (injected) return true;
        
        const heapSpan = document.getElementById('heap');
        if (!heapSpan) return false;
        
        const heapRow = heapSpan.closest('.row');
        if (!heapRow) return false;
        
        // Add stats to the existing row
        heapSpan.insertAdjacentHTML('afterend', 
            ' <span class="heap-stats" style="font-size:0.85em;color:#888;">' +
            '(5m: <span id="heapMin">---</span>-<span id="heapMax">---</span>, ' +
            'all-time min: <span id="heapAllTimeMin" style="color:#ff9800;">---</span>, ' +
            'trend: <span id="heapTrend">---</span>)</span>');
        
        heapMinEl = document.getElementById('heapMin');
        heapMaxEl = document.getElementById('heapMax');
        heapAllTimeMinEl = document.getElementById('heapAllTimeMin');
        heapTrendEl = document.getElementById('heapTrend');
        
        // Create chart container after the row
        const chartDiv = document.createElement('div');
        chartDiv.className = 'row heap-chart-row';
        chartDiv.style.cssText = 'padding: 5px 10px; background: #1a1a2e;';
        chartDiv.innerHTML = 
            '<canvas id="heapChart" width="400" height="60" style="width:100%;max-width:400px;height:60px;"></canvas>' +
            '<div style="font-size:0.7em;color:#666;text-align:right;">← 5 min | now →  (orange = all-time min, dashed = trend)</div>';
        
        heapRow.insertAdjacentElement('afterend', chartDiv);
        heapChartCanvas = document.getElementById('heapChart');
        
        injected = true;
        return true;
    }
    
    function calculateTrend() {
        if (trendSamples.length < 2) return { slope: 0, text: '---' };
        
        // Linear regression on trend samples
        const n = trendSamples.length;
        let sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
        trendSamples.forEach((s, i) => {
            sumX += i;
            sumY += s.value;
            sumXY += i * s.value;
            sumX2 += i * i;
        });
        
        const slope = (n * sumXY - sumX * sumY) / (n * sumX2 - sumX * sumX);
        const slopePerMin = slope * 6;  // Convert to per-minute (samples are 10s apart)
        
        let text, color;
        if (Math.abs(slopePerMin) < 100) {
            text = '→ stable';
            color = '#4caf50';
        } else if (slopePerMin > 0) {
            text = '↑ +' + (slopePerMin / 1024).toFixed(1) + 'KB/min';
            color = '#4caf50';
        } else {
            text = '↓ ' + (slopePerMin / 1024).toFixed(1) + 'KB/min';
            color = slopePerMin < -500 ? '#f44336' : '#ff9800';
        }
        
        return { slope, slopePerMin, text, color };
    }
    
    function updateHeapChart(heap) {
        if (!heap) return;
        if (!injectHeapChart()) return;
        
        const now = Date.now();
        
        // Update all-time minimum
        if (heap < allTimeMin) {
            allTimeMin = heap;
        }
        
        // Add to trend samples (every 10 seconds)
        if (now - lastTrendSample > TREND_INTERVAL) {
            trendSamples.push({ time: now, value: heap });
            if (trendSamples.length > TREND_SAMPLES_MAX) {
                trendSamples.shift();
            }
            lastTrendSample = now;
        }
        
        // Add to history
        heapHistory.push({ time: now, value: heap });
        if (heapHistory.length > HEAP_HISTORY_MAX) {
            heapHistory.shift();
        }
        
        // Calculate min/max of visible window
        const values = heapHistory.map(h => h.value);
        const min = Math.min(...values);
        const max = Math.max(...values);
        
        // Calculate trend
        const trend = calculateTrend();
        
        // Update display
        if (heapMinEl) heapMinEl.textContent = (min / 1024).toFixed(0) + 'KB';
        if (heapMaxEl) heapMaxEl.textContent = (max / 1024).toFixed(0) + 'KB';
        if (heapAllTimeMinEl) heapAllTimeMinEl.textContent = (allTimeMin / 1024).toFixed(0) + 'KB';
        if (heapTrendEl) {
            heapTrendEl.textContent = trend.text;
            heapTrendEl.style.color = trend.color;
        }
        
        // Draw chart
        if (!heapChartCanvas) return;
        const ctx = heapChartCanvas.getContext('2d');
        const w = heapChartCanvas.width;
        const h = heapChartCanvas.height;
        
        // Clear
        ctx.fillStyle = '#1a1a2e';
        ctx.fillRect(0, 0, w, h);
        
        if (heapHistory.length < 2) return;
        
        // Scale: include all-time min in range
        const chartMax = max * 1.05;
        const chartMin = Math.min(min, allTimeMin) * 0.95;
        const range = chartMax - chartMin || 1;
        
        // Draw all-time min line (orange dashed)
        const allTimeMinY = h - ((allTimeMin - chartMin) / range) * h;
        ctx.strokeStyle = '#ff9800';
        ctx.lineWidth = 1;
        ctx.setLineDash([4, 4]);
        ctx.beginPath();
        ctx.moveTo(0, allTimeMinY);
        ctx.lineTo(w, allTimeMinY);
        ctx.stroke();
        ctx.setLineDash([]);
        
        // Draw trend line (white dashed) if we have enough data
        if (trendSamples.length >= 10) {
            const avgValue = trendSamples.reduce((sum, s) => sum + s.value, 0) / trendSamples.length;
            const trendY = h - ((avgValue - chartMin) / range) * h;
            ctx.strokeStyle = 'rgba(255,255,255,0.3)';
            ctx.lineWidth = 1;
            ctx.setLineDash([2, 2]);
            ctx.beginPath();
            ctx.moveTo(0, trendY);
            ctx.lineTo(w, trendY);
            ctx.stroke();
            ctx.setLineDash([]);
        }
        
        // Draw main line
        ctx.strokeStyle = '#4caf50';
        ctx.lineWidth = 2;
        ctx.beginPath();
        
        const step = w / (HEAP_HISTORY_MAX - 1);
        const startIdx = HEAP_HISTORY_MAX - heapHistory.length;
        
        heapHistory.forEach((point, i) => {
            const x = (startIdx + i) * step;
            const y = h - ((point.value - chartMin) / range) * h;
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });
        ctx.stroke();
    }
    
    // Listen for status updates to update the chart
    api.registerListener((id, statusData) => {
        if (statusData.heap !== undefined) {
            updateHeapChart(statusData.heap);
        }
    }, api.EVENTS.status);
})();
