// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_SELECTION_ASK_DISPATCH_H_
#define MAHO_BROWSER_AI_MAHO_SELECTION_ASK_DISPATCH_H_

#include <string>

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

}

#endif
