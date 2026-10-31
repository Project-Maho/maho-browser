// Search recent posts on X.
browser.call("browser_navigate", { tab_id: input.tab_id, url: "https://x.com/search?f=live&q=" + encodeURIComponent(input.query) });
browser.call("page_wait_for_selector", { tab_id: input.tab_id, selector: "[data-testid='primaryColumn']", timeout_ms: 15000 });
const match = browser.call("page_query_selector", { tab_id: input.tab_id, selector: "[data-testid='primaryColumn']" });
browser.call("page_get_text", { tab_id: input.tab_id, ref_id: match.ref_id });
