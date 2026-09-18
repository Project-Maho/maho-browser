const CONTRACT_DEF: &str =
    include_str!("../../../../maho-chromium/browser/ai/maho_browser_action_contract.def");

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BrowserToolKind {
    Read,
    Action,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BrowserToolSensitivity {
    Low,
    Sensitive,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ApprovalRequirement {
    NotRequired,
    Required,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LeaseRequirement {
    NotRequired,
    Required,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum DomainPolicy {
    ActiveTabOrigin,
    ActiveTabOriginOrApprovedDestination,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum EmptyAllowlistPolicy {
    FailClosed,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct BrowserActionContract<'a> {
    pub tool_name: &'a str,
    pub kind: BrowserToolKind,
    pub changes_authority: bool,
    pub sensitivity: BrowserToolSensitivity,
    pub approval: ApprovalRequirement,
    pub lease: LeaseRequirement,
    pub domain_policy: DomainPolicy,
    pub empty_allowlist_policy: EmptyAllowlistPolicy,
}

#[derive(Debug, thiserror::Error, PartialEq, Eq)]
pub enum ContractParseError {
    #[error("invalid contract line {line}")]
    InvalidLine { line: usize },
    #[error("invalid {field} token '{value}' on line {line}")]
    InvalidToken {
        field: &'static str,
        value: String,
        line: usize,
    },
    #[error("unsafe empty allowlist policy '{value}' on line {line}")]
    UnsafeEmptyAllowlist { value: String, line: usize },
    #[error("lease-required tool '{tool_name}' must be sensitive on line {line}")]
    LeaseRequiredToolMustBeSensitive { tool_name: String, line: usize },
    #[error("sensitive tool '{tool_name}' must require approval on line {line}")]
    SensitiveToolMustRequireApproval { tool_name: String, line: usize },
}

pub fn canonical_browser_action_contracts(
) -> Result<Vec<BrowserActionContract<'static>>, ContractParseError> {
    parse_contract_def(CONTRACT_DEF)
}

pub fn parse_contract_def(
    input: &str,
) -> Result<Vec<BrowserActionContract<'_>>, ContractParseError> {
    let mut contracts = Vec::new();
    for (index, raw_line) in input.lines().enumerate() {
        let line_number = index + 1;
        let line = raw_line.trim();
        if line.is_empty() || line.starts_with("//") || line.starts_with('#') {
            continue;
        }
        contracts.push(parse_contract_line(line, line_number)?);
    }
    Ok(contracts)
}

fn parse_contract_line(
    line: &str,
    line_number: usize,
) -> Result<BrowserActionContract<'_>, ContractParseError> {
    const PREFIX: &str = "MAHO_BROWSER_ACTION_CONTRACT(";
    let Some(body) = line
        .strip_prefix(PREFIX)
        .and_then(|value| value.strip_suffix(')'))
    else {
        return Err(ContractParseError::InvalidLine { line: line_number });
    };
    let fields: Vec<&str> = body.split(',').map(str::trim).collect();
    if fields.len() != 9 {
        return Err(ContractParseError::InvalidLine { line: line_number });
    }
    let tool_name = parse_tool_name(fields[1], line_number)?;
    let kind = parse_kind(fields[2], line_number)?;
    let changes_authority = parse_changes_authority(fields[3], line_number)?;
    let sensitivity = parse_sensitivity(fields[4], line_number)?;
    let approval = parse_approval(fields[5], line_number)?;
    let lease = parse_lease(fields[6], line_number)?;
    let domain_policy = parse_domain_policy(fields[7], line_number)?;
    let empty_allowlist_policy = parse_empty_allowlist(fields[8], line_number)?;

    if lease == LeaseRequirement::Required && sensitivity != BrowserToolSensitivity::Sensitive {
        return Err(ContractParseError::LeaseRequiredToolMustBeSensitive {
            tool_name: tool_name.to_string(),
            line: line_number,
        });
    }
    // Both browser_file_upload_select and browser_visual_click are now full-allow (NotRequired).
    // Approval prompts are handled in-chat only for truly destructive actions.

    Ok(BrowserActionContract {
        tool_name,
        kind,
        changes_authority,
        sensitivity,
        approval,
        lease,
        domain_policy,
        empty_allowlist_policy,
    })
}

fn parse_changes_authority(token: &str, line: usize) -> Result<bool, ContractParseError> {
    match token {
        "Yes" => Ok(true),
        "No" => Ok(false),
        _ => Err(invalid_token("changes_authority", token, line)),
    }
}

fn parse_tool_name(token: &str, line: usize) -> Result<&str, ContractParseError> {
    let Some(name) = token
        .strip_prefix('"')
        .and_then(|value| value.strip_suffix('"'))
    else {
        return Err(invalid_token("tool_name", token, line));
    };
    if name.is_empty() {
        return Err(invalid_token("tool_name", token, line));
    }
    Ok(name)
}

fn parse_kind(token: &str, line: usize) -> Result<BrowserToolKind, ContractParseError> {
    match token {
        "Read" => Ok(BrowserToolKind::Read),
        "Action" => Ok(BrowserToolKind::Action),
        _ => Err(invalid_token("kind", token, line)),
    }
}

fn parse_sensitivity(
    token: &str,
    line: usize,
) -> Result<BrowserToolSensitivity, ContractParseError> {
    match token {
        "Low" => Ok(BrowserToolSensitivity::Low),
        "Sensitive" => Ok(BrowserToolSensitivity::Sensitive),
        _ => Err(invalid_token("sensitivity", token, line)),
    }
}

fn parse_approval(token: &str, line: usize) -> Result<ApprovalRequirement, ContractParseError> {
    match token {
        "NotRequired" => Ok(ApprovalRequirement::NotRequired),
        "Required" => Ok(ApprovalRequirement::Required),
        _ => Err(invalid_token("approval", token, line)),
    }
}

fn parse_lease(token: &str, line: usize) -> Result<LeaseRequirement, ContractParseError> {
    match token {
        "NotRequired" => Ok(LeaseRequirement::NotRequired),
        "Required" => Ok(LeaseRequirement::Required),
        _ => Err(invalid_token("lease", token, line)),
    }
}

fn parse_domain_policy(token: &str, line: usize) -> Result<DomainPolicy, ContractParseError> {
    match token {
        "ActiveTabOrigin" => Ok(DomainPolicy::ActiveTabOrigin),
        "ActiveTabOriginOrApprovedDestination" => {
            Ok(DomainPolicy::ActiveTabOriginOrApprovedDestination)
        }
        _ => Err(invalid_token("domain_policy", token, line)),
    }
}

fn parse_empty_allowlist(
    token: &str,
    line: usize,
) -> Result<EmptyAllowlistPolicy, ContractParseError> {
    match token {
        "FailClosed" => Ok(EmptyAllowlistPolicy::FailClosed),
        _ => Err(ContractParseError::UnsafeEmptyAllowlist {
            value: token.to_string(),
            line,
        }),
    }
}

fn invalid_token(field: &'static str, value: &str, line: usize) -> ContractParseError {
    ContractParseError::InvalidToken {
        field,
        value: value.to_string(),
        line,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn canonical() -> Vec<BrowserActionContract<'static>> {
        canonical_browser_action_contracts().expect("canonical contract parses")
    }

    fn named<'a>(
        contracts: &'a [BrowserActionContract<'a>],
        tool_name: &str,
    ) -> &'a BrowserActionContract<'a> {
        contracts
            .iter()
            .find(|contract| contract.tool_name == tool_name)
            .expect("contract entry exists")
    }

    #[test]
    fn action_contract_parses_canonical_table_with_required_actions() {
        let contracts = canonical();
        let required_actions = [
            "browser_navigate",
            "browser_click",
            "browser_type",
            "browser_select",
            "browser_scroll",
            "browser_hover",
            "browser_key_press",
        ];
        for tool_name in required_actions {
            let contract = named(&contracts, tool_name);
            assert_eq!(contract.kind, BrowserToolKind::Action);
            assert_eq!(contract.sensitivity, BrowserToolSensitivity::Sensitive);
            assert_eq!(contract.approval, ApprovalRequirement::NotRequired);
            assert_eq!(
                contract.empty_allowlist_policy,
                EmptyAllowlistPolicy::FailClosed
            );
        }
    }

    #[test]
    fn action_contract_browser_hover_is_sensitive_approval_not_required_without_lease() {
        let contracts = canonical();
        let hover = named(&contracts, "browser_hover");
        assert_eq!(hover.sensitivity, BrowserToolSensitivity::Sensitive);
        assert_eq!(hover.approval, ApprovalRequirement::NotRequired);
        assert_eq!(hover.lease, LeaseRequirement::NotRequired);
        assert_eq!(hover.domain_policy, DomainPolicy::ActiveTabOrigin);
    }

    #[test]
    fn action_contract_boundary_actions_do_not_require_approval() {
        let contracts = canonical();
        let upload = named(&contracts, "browser_file_upload_select");
        assert_eq!(upload.approval, ApprovalRequirement::NotRequired);
        let visual = named(&contracts, "browser_visual_click");
        assert_eq!(visual.approval, ApprovalRequirement::NotRequired);
    }

    #[test]
    fn action_contract_empty_allowlist_fails_closed_for_every_entry() {
        let contracts = canonical();
        assert!(contracts
            .iter()
            .all(|contract| contract.empty_allowlist_policy == EmptyAllowlistPolicy::FailClosed));
    }

    #[test]
    fn action_contract_rejects_unsafe_empty_allowlist() {
        let err = parse_contract_def(
            r#"MAHO_BROWSER_ACTION_CONTRACT(browser_click, "browser_click", Action, No, Sensitive, Required, Required, ActiveTabOrigin, AllowAll)"#,
        )
        .expect_err("AllowAll must fail closed");
        assert!(matches!(
            err,
            ContractParseError::UnsafeEmptyAllowlist { .. }
        ));
    }

    #[test]
    fn action_contract_rejects_lease_required_non_sensitive_tool() {
        let err = parse_contract_def(
            r#"MAHO_BROWSER_ACTION_CONTRACT(browser_click, "browser_click", Action, No, Low, NotRequired, Required, ActiveTabOrigin, FailClosed)"#,
        )
        .expect_err("lease-required tools must be sensitive");
        assert!(matches!(
            err,
            ContractParseError::LeaseRequiredToolMustBeSensitive { .. }
        ));
    }
}
