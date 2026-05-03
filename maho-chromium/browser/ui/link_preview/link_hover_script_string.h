#ifndef MAHO_BROWSER_UI_LINK_PREVIEW_LINK_HOVER_SCRIPT_STRING_H_
#define MAHO_BROWSER_UI_LINK_PREVIEW_LINK_HOVER_SCRIPT_STRING_H_

namespace maho {

inline constexpr char16_t kMahoLinkHoverScript[] = uR"js((function() {
  const HOVER_DELAY_MS = 500;
  const MESSAGE_PREFIX = 'MAHO_LINK_PREVIEW:';
  let hoverTimeout = null;
  let currentLink = null;

  function clearHoverTimeout() {
    if (hoverTimeout) {
      clearTimeout(hoverTimeout);
      hoverTimeout = null;
    }
  }

  function emit(payload) {
    console.log(MESSAGE_PREFIX + JSON.stringify(payload));
  }

  document.addEventListener('mouseover', function(event) {
    const link = event.target && event.target.closest ? event.target.closest('a[href]') : null;
    if (!link) return;
    if (currentLink === link) return;

    currentLink = link;
    clearHoverTimeout();
    hoverTimeout = setTimeout(function() {
      const rect = link.getBoundingClientRect();
      emit({
        type: 'hover',
        url: link.href,
        text: (link.textContent || '').trim().slice(0, 200),
        rect: {
          x: Math.round(rect.x),
          y: Math.round(rect.y),
          width: Math.round(rect.width),
          height: Math.round(rect.height),
        },
      });
    }, HOVER_DELAY_MS);
  }, true);

  document.addEventListener('mouseout', function(event) {
    const link = event.target && event.target.closest ? event.target.closest('a[href]') : null;
    if (!link) return;
    if (currentLink === link) currentLink = null;
    clearHoverTimeout();
    emit({type: 'dismiss'});
  }, true);
})();)js";

}  // namespace maho

#endif  // MAHO_BROWSER_UI_LINK_PREVIEW_LINK_HOVER_SCRIPT_STRING_H_
