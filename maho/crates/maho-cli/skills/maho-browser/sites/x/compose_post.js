// Draft a post in the composer. The user reviews and sends it.
browser.call("browser_navigate", { tab_id: input.tab_id, url: "https://x.com/compose/post" });
browser.call("page_wait_for_selector", { tab_id: input.tab_id, selector: "[data-testid='tweetTextarea_0']", timeout_ms: 15000 });
browser.call("browser_locator_type", { tab_id: input.tab_id, locator: { css: "[data-testid='tweetTextarea_0']" }, text: input.text });
({ drafted: true });
