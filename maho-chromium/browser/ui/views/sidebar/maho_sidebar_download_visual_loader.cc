// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ui/views/sidebar/maho_sidebar_download_visual_loader.h"

#include "base/files/file_util.h"
#include "base/functional/bind.h"
#include "base/task/thread_pool.h"
#include "chrome/browser/thumbnail/generator/image_thumbnail_request.h"
#include "ui/gfx/image/image.h"

namespace maho {

MahoSidebarDownloadVisualLoader::MahoSidebarDownloadVisualLoader() = default;

MahoSidebarDownloadVisualLoader::~MahoSidebarDownloadVisualLoader() = default;

void MahoSidebarDownloadVisualLoader::Start(const base::FilePath& file_path,
                                            int size,
                                            ThumbnailCallback callback) {
  weak_ptr_factory_.InvalidateWeakPtrs();
  callback_ = std::move(callback);

  if (file_path.empty()) {
    std::move(callback_).Run(gfx::Image());
    return;
  }

  base::ThreadPool::PostTaskAndReplyWithResult(
      FROM_HERE, {base::MayBlock(), base::TaskPriority::USER_VISIBLE},
      base::BindOnce(&base::PathExists, file_path),
      base::BindOnce(&MahoSidebarDownloadVisualLoader::OnFileExistsChecked,
                     weak_ptr_factory_.GetWeakPtr(), file_path, size));
}

void MahoSidebarDownloadVisualLoader::OnFileExistsChecked(const base::FilePath& file_path,
                                                          int size,
                                                          bool exists) {
  if (!exists) {
    if (callback_) {
      std::move(callback_).Run(gfx::Image());
    }
    return;
  }

  auto* request = new ImageThumbnailRequest(
      size, base::BindOnce(&MahoSidebarDownloadVisualLoader::OnThumbnailDecoded,
                           weak_ptr_factory_.GetWeakPtr()));
  thumbnail_request_ = request;
  request->Start(file_path);
}

void MahoSidebarDownloadVisualLoader::OnThumbnailDecoded(const SkBitmap& bitmap) {
  thumbnail_request_ = nullptr;
  if (callback_) {
    if (bitmap.empty()) {
      std::move(callback_).Run(gfx::Image());
    } else {
      std::move(callback_).Run(gfx::Image::CreateFrom1xBitmap(bitmap));
    }
  }
}

}  // namespace maho
