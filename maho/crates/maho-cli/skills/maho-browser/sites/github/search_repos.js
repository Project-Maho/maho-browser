// Search GitHub repositories and return the first page of results.
const url = "https://github.com/search?type=repositories&q=" + encodeURIComponent(input.query);
browser.call("browser_navigate", { tab_id: input.tab_id, url });
browser.call("page_wait_for_selector", { tab_id: input.tab_id, selector: "[data-testid='results-list']", timeout_ms: 15000 });
const match = browser.call("page_query_selector", { tab_id: input.tab_id, selector: "[data-testid='results-list']" });
browser.call("page_get_text", { tab_id: input.tab_id, ref_id: match.ref_id });
