// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOAD_VISUAL_LOADER_H_
#define MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOAD_VISUAL_LOADER_H_

#include <memory>
#include <string>

#include "base/files/file_path.h"
#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/image/image.h"

class ImageThumbnailRequest;

namespace maho {

class MahoSidebarDownloadVisualLoader {
 public:
   using ThumbnailCallback = base::OnceCallback<void(const gfx::Image&)>;

   MahoSidebarDownloadVisualLoader();
   MahoSidebarDownloadVisualLoader(const MahoSidebarDownloadVisualLoader&) = delete;
   MahoSidebarDownloadVisualLoader& operator=(const MahoSidebarDownloadVisualLoader&) = delete;
   ~MahoSidebarDownloadVisualLoader();

   // Starts loading the thumbnail for the given |file_path| with max dimension of |size|.
   // Invokes |callback| on completion.
   void Start(const base::FilePath& file_path, int size, ThumbnailCallback callback);

 private:
   void OnFileExistsChecked(const base::FilePath& file_path, int size, bool exists);
   void OnThumbnailDecoded(const SkBitmap& bitmap);

   raw_ptr<ImageThumbnailRequest> thumbnail_request_ = nullptr;
   ThumbnailCallback callback_;

   base::WeakPtrFactory<MahoSidebarDownloadVisualLoader> weak_ptr_factory_{this};
 };

}  // namespace maho

#endif  // MAHO_BROWSER_UI_VIEWS_SIDEBAR_MAHO_SIDEBAR_DOWNLOAD_VISUAL_LOADER_H_
