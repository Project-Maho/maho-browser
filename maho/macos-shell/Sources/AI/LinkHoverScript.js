(function() {
    let hoverTimeout = null;
    document.addEventListener('mouseover', function(e) {
        const link = e.target.closest('a[href]');
        if (!link) return;
        clearTimeout(hoverTimeout);
        hoverTimeout = setTimeout(function() {
            window.webkit.messageHandlers.linkHover.postMessage({
                url: link.href,
                text: link.textContent.substring(0, 200),
                rect: JSON.stringify(link.getBoundingClientRect())
            });
        }, 300);
    });
    document.addEventListener('mouseout', function(e) {
        if (e.target.closest('a[href]')) {
            clearTimeout(hoverTimeout);
            window.webkit.messageHandlers.linkHover.postMessage({ cancelled: true });
        }
    });
})();
