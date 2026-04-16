/**
 * Halmet Dashboard - Init
 * 
 * Creates a new "N2K Stream" tab page using the esp32nmea2k API.
 * This avoids conflict with the existing dashboardPage polling.
 * Requires core and components to be loaded first.
 */

(function() {
    const api = window.esp32nmea2k;
    if (!api) return;

    async function initDashboard(container) {
        try {
            await hdLoadDependencies();
            render(html`<${MessageLogger} />`, container);
        } catch (err) {
            console.error('Dashboard init failed:', err);
            container.innerHTML = '<div class="error">Failed to load dashboard. Check internet connection for CDN access.</div>';
        }
    }

    api.registerListener((id, data) => {
        // Create a new tab page - this won't have the boatDataString polling
        const streamPage = api.addTabPage('n2kStreamPage', 'N2K Stream');
        if (!streamPage) {
            console.error('Dashboard: Failed to create tab page');
            return;
        }
        
        const container = document.createElement('div');
        container.id = 'halmet-preact-root';
        streamPage.appendChild(container);

        let initialized = false;
        
        api.registerListener((tabId, tabData) => {
            if (tabData === 'n2kStreamPage' && !initialized) {
                initialized = true;
                initDashboard(container);
            }
        }, api.EVENTS.tab);

    }, api.EVENTS.init);
})();
