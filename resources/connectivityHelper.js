// Simple connectivity helper - pure JS implementation
window.jmpCheckServerConnectivity = (() => {
    let activeController = null;

    const checkFunc = async function(url) {
        // Abort any in-progress check
        if (activeController) {
            activeController.abort();
        }

        // Create abort controller for this check
        const controller = new AbortController();
        activeController = controller;

        try {
            // Normalize URL
            if (!url.startsWith('http://') && !url.startsWith('https://')) {
                url = 'http://' + url;
            }

            // Try to fetch server info
            const infoUrl = url.replace(/\/$/, '') + '/System/Info/Public';
            console.log('Checking connectivity:', infoUrl);

            const response = await fetch(infoUrl, {
                signal: controller.signal,
                mode: 'cors',
                credentials: 'omit'
            });

            if (!response.ok) {
                throw new Error('Server returned ' + response.status);
            }

            const info = await response.json();
            console.log('Server info:', info);

            // Return the base URL (handle redirects by using response.url)
            const resolvedUrl = new URL(response.url);
            const baseUrl = resolvedUrl.origin + resolvedUrl.pathname.replace('/System/Info/Public', '');

            activeController = null;
            return baseUrl.replace(/\/$/, '');
        } catch (e) {
            activeController = null;
            if (e.name === 'AbortError') {
                throw new Error('Connection cancelled');
            }
            throw e;
        }
    };

    checkFunc.abort = () => {
        if (activeController) {
            activeController.abort();
            activeController = null;
        }
    };

    return checkFunc;
})();
