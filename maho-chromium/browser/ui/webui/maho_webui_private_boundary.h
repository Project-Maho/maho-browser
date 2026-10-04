#ifndef MAHO_BROWSER_UI_WEBUI_MAHO_WEBUI_PRIVATE_BOUNDARY_H_
#define MAHO_BROWSER_UI_WEBUI_MAHO_WEBUI_PRIVATE_BOUNDARY_H_

namespace content {
class BrowserContext;
}

bool MahoIsWebUIEnabled(content::BrowserContext* context);

#endif  // MAHO_BROWSER_UI_WEBUI_MAHO_WEBUI_PRIVATE_BOUNDARY_H_
