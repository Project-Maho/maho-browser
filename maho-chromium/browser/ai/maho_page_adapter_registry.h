// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_PAGE_ADAPTER_REGISTRY_H_
#define MAHO_BROWSER_AI_MAHO_PAGE_ADAPTER_REGISTRY_H_

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace maho::ai {

/// Canonicalizes a URL or origin string to its standard `scheme://host[:port]` representation.
/// Returns std::nullopt if the input is not a valid HTTP/HTTPS URL or origin.
/// Ports default to 80 (http) and 443 (https) and are omitted in standard canonical origin.
std::optional<std::string> CanonicalOrigin(std::string_view url_or_origin);

/// Checks whether a given origin or URL strictly matches an exact-origin pattern.
/// Substring/suffix matches (e.g. `docs.google.com.evil.io`), scheme mismatches,
/// and port mismatches fail closed and return false.
bool MatchesExactOrigin(std::string_view pattern, std::string_view candidate);

/// Metadata descriptor for a registered page adapter.
struct PageAdapterDescriptor {
  PageAdapterDescriptor();
  PageAdapterDescriptor(
      std::string id,
      std::vector<std::string> origins,
      std::vector<std::string> operations,
      std::optional<std::string> world = std::nullopt,
      std::optional<size_t> max_bytes = std::nullopt);
  ~PageAdapterDescriptor();
  PageAdapterDescriptor(const PageAdapterDescriptor&);
  PageAdapterDescriptor& operator=(const PageAdapterDescriptor&);
  PageAdapterDescriptor(PageAdapterDescriptor&&);
  PageAdapterDescriptor& operator=(PageAdapterDescriptor&&);

  std::string adapter_id;
  std::vector<std::string> exact_origin_patterns;
  std::vector<std::string> supported_operations;
  std::optional<std::string> execution_world;
  std::optional<size_t> max_output_bytes;

  bool operator==(const PageAdapterDescriptor& other) const = default;
};

/// Base interface implemented by exact-origin page adapters.
class PageAdapter {
 public:
  virtual ~PageAdapter() = default;

  /// Unique identifier for this adapter.
  virtual const std::string& id() const = 0;

  /// List of exact origin patterns (e.g. `{"https://docs.google.com"}`) supported by this adapter.
  virtual const std::vector<std::string>& exact_origins() const = 0;

  /// List of operation names (e.g. `{"get_selection", "insert_text"}`) supported by this adapter.
  virtual const std::vector<std::string>& supported_operations() const = 0;

  /// Optional isolated execution world override.
  virtual const std::optional<std::string>& execution_world() const;

  /// Optional bounded payload size limit.
  virtual const std::optional<size_t>& max_output_bytes() const;

  /// Checks if this adapter matches the given origin or URL.
  virtual bool MatchesOrigin(std::string_view origin_or_url) const;

  /// Checks if this adapter supports the given operation name.
  virtual bool SupportsOperation(std::string_view operation) const;

  /// Returns the descriptor for this adapter.
  virtual PageAdapterDescriptor descriptor() const;
};

/// A descriptor-backed page adapter implementation.
class SimplePageAdapter : public PageAdapter {
 public:
  SimplePageAdapter(std::string id,
                    std::vector<std::string> exact_origins,
                    std::vector<std::string> supported_operations,
                    std::optional<std::string> execution_world = std::nullopt,
                    std::optional<size_t> max_output_bytes = std::nullopt);
  explicit SimplePageAdapter(PageAdapterDescriptor descriptor);
  ~SimplePageAdapter() override;

  const std::string& id() const override;
  const std::vector<std::string>& exact_origins() const override;
  const std::vector<std::string>& supported_operations() const override;
  const std::optional<std::string>& execution_world() const override;
  const std::optional<size_t>& max_output_bytes() const override;
  PageAdapterDescriptor descriptor() const override;

 private:
  PageAdapterDescriptor descriptor_;
};

/// Registry holding exact-origin page adapters with insertion-order determinism.
class MahoPageAdapterRegistry {
 public:
  MahoPageAdapterRegistry();
  ~MahoPageAdapterRegistry();

  MahoPageAdapterRegistry(const MahoPageAdapterRegistry&) = delete;
  MahoPageAdapterRegistry& operator=(const MahoPageAdapterRegistry&) = delete;
  MahoPageAdapterRegistry(MahoPageAdapterRegistry&&) noexcept;
  MahoPageAdapterRegistry& operator=(MahoPageAdapterRegistry&&) noexcept;

  /// Registers an adapter instance, preserving insertion order.
  void Register(std::shared_ptr<PageAdapter> adapter);

  /// Registers a descriptor-backed adapter, preserving insertion order.
  void RegisterDescriptor(PageAdapterDescriptor descriptor);

  /// Convenience method to register a simple adapter by parts.
  void RegisterSimple(std::string id,
                      std::vector<std::string> exact_origins,
                      std::vector<std::string> supported_operations,
                      std::optional<std::string> execution_world = std::nullopt,
                      std::optional<size_t> max_output_bytes = std::nullopt);

  /// Looks up the first matching adapter for `(origin_or_url, operation)` in insertion order.
  /// Returns nullptr if origin does not match or operation is unsupported.
  std::shared_ptr<PageAdapter> Lookup(std::string_view origin_or_url,
                                      std::string_view operation) const;

  /// Alias for Lookup.
  std::shared_ptr<PageAdapter> FindAdapter(std::string_view origin_or_url,
                                           std::string_view operation) const {
    return Lookup(origin_or_url, operation);
  }

  /// Alias for Lookup.
  std::shared_ptr<PageAdapter> Resolve(std::string_view origin_or_url,
                                       std::string_view operation) const {
    return Lookup(origin_or_url, operation);
  }

  /// Returns all registered adapters in insertion order.
  const std::vector<std::shared_ptr<PageAdapter>>& adapters() const {
    return adapters_;
  }

  /// Returns the count of registered adapters.
  size_t size() const { return adapters_.size(); }

  /// Returns true if no adapters are registered.
  bool empty() const { return adapters_.empty(); }

  /// Clears all registered adapters.
  void Clear() { adapters_.clear(); }

  /// Returns the process-global singleton registry instance.
  static MahoPageAdapterRegistry& GetGlobalRegistry();

 private:
  std::vector<std::shared_ptr<PageAdapter>> adapters_;
};

using PageAdapterRegistry = MahoPageAdapterRegistry;

/// Browser tool executor seam function.
///
/// EXECUTOR INTEGRATION CALL SITE NOTE:
/// In `MahoBrowserToolExecutor` (e.g. `ExecuteBrowserActionContract` or action dispatch),
/// before falling back to generic accessibility/DOM element tree execution, the executor
/// queries this seam with the target tab's active origin and requested operation:
///
/// ```cpp
/// if (auto adapter = maho::ai::LookupPageAdapterForSeam(active_origin, operation_name)) {
///   // Found specialized exact-origin adapter (e.g. Google Docs canvas, rich grid)
///   ExecutePageAdapterAction(std::move(adapter), operation_name, arguments, std::move(callback));
///   return;
/// }
/// // Otherwise, proceed with generic DOM fallback tier.
/// ```
std::shared_ptr<PageAdapter> LookupPageAdapterForSeam(
    std::string_view origin_or_url,
    std::string_view operation);

}  // namespace maho::ai

#endif  // MAHO_BROWSER_AI_MAHO_PAGE_ADAPTER_REGISTRY_H_
