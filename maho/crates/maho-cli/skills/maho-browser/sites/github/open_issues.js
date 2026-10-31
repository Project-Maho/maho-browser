// List open issues of owner/repo.
browser.call("browser_navigate", { tab_id: input.tab_id, url: "https://github.com/" + input.repo + "/issues?q=is%3Aissue+is%3Aopen" });
browser.call("page_wait_for_selector", { tab_id: input.tab_id, selector: "main", timeout_ms: 15000 });
const match = browser.call("page_query_selector", { tab_id: input.tab_id, selector: "main" });
browser.call("page_get_text", { tab_id: input.tab_id, ref_id: match.ref_id });
