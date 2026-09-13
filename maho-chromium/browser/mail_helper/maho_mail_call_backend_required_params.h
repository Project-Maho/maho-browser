// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_CALL_BACKEND_REQUIRED_PARAMS_H_
#define MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_CALL_BACKEND_REQUIRED_PARAMS_H_

#include <optional>
#include <string>

namespace maho {

// `MahoMailHelperImpl::CallBackend()` dispatches on `command` and pulls its
// arguments out of `args_json` via silent-default helpers (get_str/get_int),
// so a caller that forgets a required field (e.g. "email_id") used to make a
// live backend/FFI call with an empty string instead of failing fast.
//
// This function is the single required-parameter gate for that dispatcher.
// It re-parses `args_json` independently (no dependency on CallBackend's
// internal lambdas) so it can be unit tested in isolation.
//
// Returns std::nullopt when every required parameter for `command` is
// present and non-empty in `args_json` (this also covers commands that take
// only optional/defaulted parameters, and unknown commands, which
// CallBackend rejects itself downstream).
//
// Returns the name of the first missing/empty required parameter otherwise;
// CallBackend must turn that into an explicit invalid-argument error instead
// of silently substituting a default.
std::optional<std::string> FindMissingRequiredCallBackendParam(
    const std::string& command,
    const std::string& args_json);

}  // namespace maho

#endif  // MAHO_BROWSER_MAIL_HELPER_MAHO_MAIL_CALL_BACKEND_REQUIRED_PARAMS_H_
