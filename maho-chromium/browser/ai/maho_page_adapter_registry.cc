#include "base/no_destructor.h"
// Copyright 2026 Maho Browser. All rights reserved.

#include "maho/browser/ai/maho_page_adapter_registry.h"

#include <algorithm>
#include <cctype>

namespace maho::ai {

MahoPageAdapterRegistry::MahoPageAdapterRegistry(MahoPageAdapterRegistry&&) noexcept = default;
MahoPageAdapterRegistry& MahoPageAdapterRegistry::operator=(MahoPageAdapterRegistry&&) noexcept = default;

PageAdapterDescriptor::PageAdapterDescriptor() = default;
PageAdapterDescriptor::PageAdapterDescriptor(
    std::string id,
    std::vector<std::string> origins,
    std::vector<std::string> operations,
    std::optional<std::string> world,
    std::optional<size_t> max_bytes)
    : adapter_id(std::move(id)),
      exact_origin_patterns(std::move(origins)),
      supported_operations(std::move(operations)),
      execution_world(std::move(world)),
      max_output_bytes(max_bytes) {}
PageAdapterDescriptor::~PageAdapterDescriptor() = default;
PageAdapterDescriptor::PageAdapterDescriptor(const PageAdapterDescriptor&) = default;
PageAdapterDescriptor& PageAdapterDescriptor::operator=(const PageAdapterDescriptor&) = default;
PageAdapterDescriptor::PageAdapterDescriptor(PageAdapterDescriptor&&) = default;
PageAdapterDescriptor& PageAdapterDescriptor::operator=(PageAdapterDescriptor&&) = default;

namespace {

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string_view TrimWhitespace(std::string_view sv) {
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) {
    sv.remove_suffix(1);
  }
  return sv;
}

}  // namespace

std::optional<std::string> CanonicalOrigin(std::string_view url_or_origin) {
  std::string_view sv = TrimWhitespace(url_or_origin);
  if (sv.empty()) {
    return std::nullopt;
  }

  std::string scheme;
  std::string_view rest;
  if (sv.size() >= 7 && EqualsIgnoreCase(sv.substr(0, 7), "http://")) {
    scheme = "http";
    rest = sv.substr(7);
  } else if (sv.size() >= 8 && EqualsIgnoreCase(sv.substr(0, 8), "https://")) {
    scheme = "https";
    rest = sv.substr(8);
  } else {
    return std::nullopt;
  }

  // Find the end of authority (host + optional port) marked by '/', '?', or '#'
  size_t authority_end = rest.find_first_of("/?#");
  std::string_view authority = (authority_end == std::string_view::npos)
                                   ? rest
                                   : rest.substr(0, authority_end);

  if (authority.empty()) {
    return std::nullopt;
  }

  // Handle optional userinfo (e.g. user:pass@host)
  size_t at_pos = authority.rfind('@');
  if (at_pos != std::string_view::npos) {
    authority = authority.substr(at_pos + 1);
  }

  if (authority.empty()) {
    return std::nullopt;
  }

  std::string_view host_part;
  std::optional<int> parsed_port;

  // Check for IPv6 bracketed host like [::1]:8080
  if (authority.front() == '[') {
    size_t closing_bracket = authority.find(']');
    if (closing_bracket == std::string_view::npos) {
      return std::nullopt;
    }
    host_part = authority.substr(0, closing_bracket + 1);
    std::string_view after_bracket = authority.substr(closing_bracket + 1);
    if (!after_bracket.empty()) {
      if (after_bracket.front() != ':') {
        return std::nullopt;
      }
      std::string_view port_str = after_bracket.substr(1);
      if (port_str.empty()) {
        return std::nullopt;
      }
      int port = 0;
      for (char c : port_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          return std::nullopt;
        }
        port = port * 10 + (c - '0');
        if (port > 65535) {
          return std::nullopt;
        }
      }
      if (port == 0) {
        return std::nullopt;
      }
      parsed_port = port;
    }
  } else {
    size_t colon_pos = authority.find(':');
    if (colon_pos != std::string_view::npos) {
      host_part = authority.substr(0, colon_pos);
      std::string_view port_str = authority.substr(colon_pos + 1);
      if (port_str.empty()) {
        return std::nullopt;
      }
      int port = 0;
      for (char c : port_str) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
          return std::nullopt;
        }
        port = port * 10 + (c - '0');
        if (port > 65535) {
          return std::nullopt;
        }
      }
      if (port == 0) {
        return std::nullopt;
      }
      parsed_port = port;
    } else {
      host_part = authority;
    }
  }

  if (host_part.empty()) {
    return std::nullopt;
  }

  // Canonicalize host to lowercase
  std::string lower_host;
  lower_host.reserve(host_part.size());
  for (char c : host_part) {
    // Basic valid host character check
    if (std::isspace(static_cast<unsigned char>(c)) || c == '/' || c == '?' ||
        c == '#' || c == '\\') {
      return std::nullopt;
    }
    lower_host.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }

  // Default port normalization
  if (parsed_port.has_value()) {
    if ((scheme == "http" && *parsed_port == 80) ||
        (scheme == "https" && *parsed_port == 443)) {
      parsed_port = std::nullopt;
    }
  }

  std::string result = scheme + "://" + lower_host;
  if (parsed_port.has_value()) {
    result += ":" + std::to_string(*parsed_port);
  }
  return result;
}

bool MatchesExactOrigin(std::string_view pattern, std::string_view candidate) {
  auto pat = CanonicalOrigin(pattern);
  auto cand = CanonicalOrigin(candidate);
  if (!pat.has_value() || !cand.has_value()) {
    return false;
  }
  return *pat == *cand;
}

const std::optional<std::string>& PageAdapter::execution_world() const {
  static const std::optional<std::string> kNone = std::nullopt;
  return kNone;
}

const std::optional<size_t>& PageAdapter::max_output_bytes() const {
  static const std::optional<size_t> kNone = std::nullopt;
  return kNone;
}

bool PageAdapter::MatchesOrigin(std::string_view origin_or_url) const {
  auto target = CanonicalOrigin(origin_or_url);
  if (!target.has_value()) {
    return false;
  }
  for (const auto& pattern : exact_origins()) {
    auto pat = CanonicalOrigin(pattern);
    if (pat.has_value() && *pat == *target) {
      return true;
    }
  }
  return false;
}

bool PageAdapter::SupportsOperation(std::string_view operation) const {
  for (const auto& op : supported_operations()) {
    if (op == operation) {
      return true;
    }
  }
  return false;
}

PageAdapterDescriptor PageAdapter::descriptor() const {
  PageAdapterDescriptor desc;
  desc.adapter_id = id();
  desc.exact_origin_patterns = exact_origins();
  desc.supported_operations = supported_operations();
  desc.execution_world = execution_world();
  desc.max_output_bytes = max_output_bytes();
  return desc;
}

SimplePageAdapter::SimplePageAdapter(
    std::string id,
    std::vector<std::string> exact_origins,
    std::vector<std::string> supported_operations,
    std::optional<std::string> execution_world,
    std::optional<size_t> max_output_bytes) {
  descriptor_.adapter_id = std::move(id);
  descriptor_.exact_origin_patterns = std::move(exact_origins);
  descriptor_.supported_operations = std::move(supported_operations);
  descriptor_.execution_world = std::move(execution_world);
  descriptor_.max_output_bytes = max_output_bytes;
}

SimplePageAdapter::SimplePageAdapter(PageAdapterDescriptor descriptor)
    : descriptor_(std::move(descriptor)) {}

SimplePageAdapter::~SimplePageAdapter() = default;

const std::string& SimplePageAdapter::id() const {
  return descriptor_.adapter_id;
}

const std::vector<std::string>& SimplePageAdapter::exact_origins() const {
  return descriptor_.exact_origin_patterns;
}

const std::vector<std::string>& SimplePageAdapter::supported_operations()
    const {
  return descriptor_.supported_operations;
}

const std::optional<std::string>& SimplePageAdapter::execution_world() const {
  return descriptor_.execution_world;
}

const std::optional<size_t>& SimplePageAdapter::max_output_bytes() const {
  return descriptor_.max_output_bytes;
}

PageAdapterDescriptor SimplePageAdapter::descriptor() const {
  return descriptor_;
}

MahoPageAdapterRegistry::MahoPageAdapterRegistry() = default;
MahoPageAdapterRegistry::~MahoPageAdapterRegistry() = default;

void MahoPageAdapterRegistry::Register(std::shared_ptr<PageAdapter> adapter) {
  if (adapter) {
    adapters_.push_back(std::move(adapter));
  }
}

void MahoPageAdapterRegistry::RegisterDescriptor(
    PageAdapterDescriptor descriptor) {
  adapters_.push_back(
      std::make_shared<SimplePageAdapter>(std::move(descriptor)));
}

void MahoPageAdapterRegistry::RegisterSimple(
    std::string id,
    std::vector<std::string> exact_origins,
    std::vector<std::string> supported_operations,
    std::optional<std::string> execution_world,
    std::optional<size_t> max_output_bytes) {
  adapters_.push_back(std::make_shared<SimplePageAdapter>(
      std::move(id), std::move(exact_origins), std::move(supported_operations),
      std::move(execution_world), max_output_bytes));
}

std::shared_ptr<PageAdapter> MahoPageAdapterRegistry::Lookup(
    std::string_view origin_or_url,
    std::string_view operation) const {
  auto target_origin = CanonicalOrigin(origin_or_url);
  if (!target_origin.has_value()) {
    return nullptr;
  }

  for (const auto& adapter : adapters_) {
    if (!adapter) {
      continue;
    }
    bool origin_matches = false;
    for (const auto& pattern : adapter->exact_origins()) {
      auto pat = CanonicalOrigin(pattern);
      if (pat.has_value() && *pat == *target_origin) {
        origin_matches = true;
        break;
      }
    }
    if (origin_matches && adapter->SupportsOperation(operation)) {
      return adapter;
    }
  }
  return nullptr;
}

// static
MahoPageAdapterRegistry& MahoPageAdapterRegistry::GetGlobalRegistry() {
  static base::NoDestructor<MahoPageAdapterRegistry> instance;
  return *instance;
}

std::shared_ptr<PageAdapter> LookupPageAdapterForSeam(
    std::string_view origin_or_url,
    std::string_view operation) {
  return MahoPageAdapterRegistry::GetGlobalRegistry().Lookup(origin_or_url,
                                                             operation);
}

}  // namespace maho::ai
