// Run a Google search and return the result column text.
browser.call("browser_navigate", { tab_id: input.tab_id, url: "https://www.google.com/search?q=" + encodeURIComponent(input.query) });
browser.call("page_wait_for_selector", { tab_id: input.tab_id, selector: "#search", timeout_ms: 15000 });
const match = browser.call("page_query_selector", { tab_id: input.tab_id, selector: "#search" });
browser.call("page_get_text", { tab_id: input.tab_id, ref_id: match.ref_id });
