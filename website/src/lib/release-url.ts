export function releasePageUrl(releaseRepositoryUrl: string): string {
  return `${releaseRepositoryUrl.replace(/\/$/, "")}/releases`;
}

export function latestReleaseAssetUrl(
  releaseRepositoryUrl: string,
  assetName: string,
): string {
  return `${releaseRepositoryUrl.replace(/\/$/, "")}/releases/latest/download/${encodeURIComponent(assetName)}`;
}
