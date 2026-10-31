// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_
#define MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/containers/span.h"
#include "base/memory/raw_ptr.h"
#include "base/values.h"

struct MahoCore;

namespace maho::ai {
struct BrowserActionContract;
}

class MahoBrowserToolRegistry {
public:
  inline static constexpr int kCatalogVersion = 1;
  inline static constexpr int kSchemaVersion = 1;
  inline static constexpr int kResultVersion = 1;

  enum class Category {
    kUnknown,
    kTabs,
    kNavigation,
    kPage,
    kInput,
    kHistory,
    kBookmarks,
    kPolicy,
    kRoutines,
    kMail,
    kLease,
    kVault,
    kCapture,
  };
  enum class Mutability { kUnknown, kReadOnly, kMutable };
  enum class Broker { kUnknown, kBrowser, kMail, kRoutines, kVault };
  enum class Boundary { kUnknown, kSession, kProfile, kTab };
  enum class Sensitivity { kUnknown, kLow, kSensitive, kCredential };
  enum class FeatureGate {
    kUnknown,
    kAlways,
    kMailBeta,
    kRoutines,
    kVault,
    kNativeInput,
  };
  enum class MissingPolicy { kUnknown, kFailClosed };
  enum Surface : uint32_t {
    kDesktopAgent = 1u << 0,
    kPublicMcp = 1u << 1,
    kControlPlane = 1u << 2,
    kCliAgent = 1u << 3,
    kCliGeneric = 1u << 4,
    // Transitional spelling retained for existing registry callers.
    kBrowserMcp = kPublicMcp,
  };
  using SurfaceMask = uint32_t;

  struct CapabilityDescriptor {
    CapabilityDescriptor(
        std::string_view canonical_id,
        std::string_view tool_name,
        int schema_version,
        int result_version,
        Category category,
        Mutability mutability,
        bool changes_authority,
        Broker required_broker,
        Boundary required_boundary,
        Sensitivity sensitivity,
        FeatureGate feature_gate,
        SurfaceMask surfaces,
        MissingPolicy missing_policy,
        std::string_view description,
        const maho::ai::BrowserActionContract* browser_action_contract);
    CapabilityDescriptor(const CapabilityDescriptor&);
    CapabilityDescriptor& operator=(const CapabilityDescriptor&);
    CapabilityDescriptor(CapabilityDescriptor&&);
    CapabilityDescriptor& operator=(CapabilityDescriptor&&);
    ~CapabilityDescriptor();

    std::string_view canonical_id;
    std::string_view tool_name;
    int schema_version;
    int result_version;
    Category category;
    Mutability mutability;
    bool changes_authority;
    Broker required_broker;
    Boundary required_boundary;
    Sensitivity sensitivity;
    FeatureGate feature_gate;
    SurfaceMask surfaces;
    MissingPolicy missing_policy;
    std::string_view description;
    raw_ptr<const maho::ai::BrowserActionContract> browser_action_contract =
        nullptr;
  };

  struct ExecutionReceiptContext {
    ExecutionReceiptContext();
    ~ExecutionReceiptContext();

    std::string execution_id;
    std::string controller_id;
    std::string controller_name;
    std::string controller_type;
    std::string control_plane;
    std::optional<int> target_tab_id;
    std::optional<std::string> target_origin;
    std::string approval;
    std::string outcome_status;
    std::string outcome_code;
    double started_at_seconds = 0;
    double completed_at_seconds = 0;
  };

  struct ToolSchema {
    ToolSchema();
    ToolSchema(const ToolSchema &) = delete;
    ToolSchema(ToolSchema &&);
    ToolSchema &operator=(const ToolSchema &) = delete;
    ToolSchema &operator=(ToolSchema &&);
    ~ToolSchema();

    std::string name;
    std::string description;
    base::DictValue input_schema;
    raw_ptr<const maho::ai::BrowserActionContract> browser_action_contract =
        nullptr;
  };

  static const std::vector<CapabilityDescriptor> &GetCapabilityDescriptors();
  static bool ValidateCapabilityDescriptors(
      base::span<const CapabilityDescriptor> descriptors);
  static const CapabilityDescriptor *FindCapability(std::string_view tool_name);
  static const CapabilityDescriptor *
  FindCapabilityById(std::string_view canonical_id);
  static std::vector<const CapabilityDescriptor *>
  GetCapabilitiesForSurface(Surface surface, bool mail_enabled = true,
                            bool routines_enabled = true,
                            bool vault_enabled = true,
                            bool native_input_enabled = false);
  static std::optional<base::DictValue>
  SerializePublicMcpCapability(const CapabilityDescriptor &descriptor);
  static std::optional<base::DictValue>
  SerializeControlPlaneCapability(const CapabilityDescriptor &descriptor);
  static base::ListValue
  SerializePublicMcpCapabilities(bool mail_enabled = true,
                                 bool routines_enabled = true,
                                 bool vault_enabled = true,
                                 bool native_input_enabled = false);
  static base::DictValue SerializeDiagnostics(
      Surface surface,
      bool mail_enabled = true,
      bool routines_enabled = true,
      bool vault_enabled = true,
      bool native_input_enabled = false);
  static base::ListValue SerializeAgentCapabilities(
      bool mail_enabled = true, bool routines_enabled = true,
      bool vault_enabled = true,
      bool native_input_enabled = false);
  static base::DictValue SerializeExecutionResult(
      const CapabilityDescriptor &descriptor, std::string output_json,
      const ExecutionReceiptContext &context);

  static const std::vector<ToolSchema> &GetPhase1ToolSchemas();
  static const ToolSchema *FindToolSchema(const std::string &tool_name);
  static base::ListValue SerializePhase1ToolSchemas(bool enabled = true);
  static bool IsBrowserActionTool(const std::string &tool_name);

  // Routines CRUD execution and validation bridges.
  static bool IsRoutineEventTriggerSupported();
  static bool ExecuteRoutineCreate(const base::DictValue& params,
                                   MahoCore* core = nullptr,
                                   std::string* error_out = nullptr);
  static bool ExecuteRoutineUpdate(const base::DictValue& params,
                                   MahoCore* core = nullptr,
                                   std::string* error_out = nullptr);
  static bool ExecuteRoutineDelete(std::string_view routine_id,
                                   MahoCore* core = nullptr,
                                   std::string* error_out = nullptr);
  static std::string ExecuteRoutineHistory(
      std::optional<std::string_view> routine_id = std::nullopt,
      uint32_t limit = 50,
      MahoCore* core = nullptr);

  // Human help request dispatch and validation bridge.
  static base::DictValue ExecuteBrowserRequestHelp(
      const base::DictValue& params,
      std::string* error_out = nullptr);

  static constexpr bool HasSurface(const CapabilityDescriptor &descriptor,
                                   Surface surface) {
    return (descriptor.surfaces & static_cast<SurfaceMask>(surface)) != 0;
  }
};

#endif // MAHO_BROWSER_AI_MAHO_BROWSER_TOOL_REGISTRY_H_
