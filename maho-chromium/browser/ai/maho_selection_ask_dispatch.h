// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_SELECTION_ASK_DISPATCH_H_
#define MAHO_BROWSER_AI_MAHO_SELECTION_ASK_DISPATCH_H_

#include <cstdint>
#include <string>
#include <vector>

class BrowserWindowInterface;
class GURL;

namespace content {
class WebContents;
}

namespace maho {

// Opens the Maho AI panel with a question about |selection| on |page_url|.
// No-op for off-the-record profiles, an empty selection or no browser.
void AskMahoAboutSelection(BrowserWindowInterface* browser,
                           content::WebContents* web_contents,
                           const GURL& page_url,
                           const std::u16string& selection);

// Opens the Maho AI panel with |png_bytes| (a capture of |page_url| the user
// just took) attached as an image. The bytes stay in the browser: the panel
// receives a single-use token. Returns false (and dispatches nothing) for
// off-the-record profiles, no browser, or bytes that are not a PNG within the
// attachment size cap.
bool AskMahoAboutCapture(BrowserWindowInterface* browser,
                         content::WebContents* web_contents,
                         const GURL& page_url,
                         std::vector<uint8_t> png_bytes);

}

#endif
