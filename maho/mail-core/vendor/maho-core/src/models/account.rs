use serde::{Deserialize, Serialize};

/// Encryption mode for IMAP/SMTP connections.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub enum Encryption {
    None,
    Tls,
    StartTls,
}

impl Encryption {
    pub fn as_str(&self) -> &str {
        match self {
            Encryption::None => "none",
            Encryption::Tls => "tls",
            Encryption::StartTls => "starttls",
        }
    }

    pub fn from_str_lossy(s: &str) -> Self {
        match s.to_ascii_lowercase().as_str() {
            "tls" => Encryption::Tls,
            "starttls" => Encryption::StartTls,
            _ => Encryption::None,
        }
    }
}

/// Full account record stored in the database.
/// Contains sensitive credentials that are never serialized to frontend responses.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Account {
    pub id: String,
    pub email: String,
    pub display_name: String,
    pub auth_type: String,
    pub imap_host: String,
    pub imap_port: u16,
    pub imap_encryption: Encryption,
    pub smtp_host: String,
    pub smtp_port: u16,
    pub smtp_encryption: Encryption,
    pub username: String,
    pub oauth2_client_id: Option<String>,
    #[serde(skip_serializing)]
    pub oauth2_client_secret: Option<String>,
    #[serde(skip_serializing)]
    pub oauth2_refresh_token: Option<String>,
    #[serde(skip_serializing)]
    pub oauth2_access_token: Option<String>,
    #[serde(skip_serializing)]
    pub oauth2_expires_at: Option<String>,
    #[serde(skip_serializing)]
    pub password: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

impl Account {
    #[allow(dead_code)]
    pub fn from_row(row: &rusqlite::Row) -> Result<Self, rusqlite::Error> {
        let auth_type: Option<String> = row.get("auth_type")?;
        let imap_encryption_str: String = row.get("imap_encryption")?;
        let smtp_encryption_str: String = row.get("smtp_encryption")?;
        let imap_port: i64 = row.get("imap_port")?;
        let smtp_port: i64 = row.get("smtp_port")?;

        Ok(Account {
            id: row.get("id")?,
            email: row.get("email")?,
            display_name: row.get("display_name")?,
            auth_type: auth_type.unwrap_or_else(|| "password".to_string()),
            imap_host: row.get("imap_host")?,
            imap_port: imap_port as u16,
            imap_encryption: Encryption::from_str_lossy(&imap_encryption_str),
            smtp_host: row.get("smtp_host")?,
            smtp_port: smtp_port as u16,
            smtp_encryption: Encryption::from_str_lossy(&smtp_encryption_str),
            username: row.get("username")?,
            oauth2_client_id: row.get("oauth2_client_id")?,
            oauth2_client_secret: row.get("oauth2_client_secret")?,
            oauth2_refresh_token: row.get("oauth2_refresh_token")?,
            oauth2_access_token: row.get("oauth2_access_token")?,
            oauth2_expires_at: row.get("oauth2_expires_at")?,
            password: row.get("password")?,
            created_at: row.get("created_at")?,
            updated_at: row.get("updated_at")?,
        })
    }
}

/// Request payload for creating a new account.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CreateAccountRequest {
    pub email: String,
    pub display_name: String,
    pub auth_type: Option<String>,
    pub imap_host: String,
    pub imap_port: u16,
    pub imap_encryption: Encryption,
    pub smtp_host: String,
    pub smtp_port: u16,
    pub smtp_encryption: Encryption,
    pub username: String,
    pub password: Option<String>,
    pub oauth2_client_id: Option<String>,
    pub oauth2_client_secret: Option<String>,
    pub oauth2_access_token: Option<String>,
    pub oauth2_refresh_token: Option<String>,
    pub oauth2_expires_at: Option<String>,
}

/// Request payload for updating an existing account.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct UpdateAccountRequest {
    pub id: String,
    pub email: Option<String>,
    pub display_name: Option<String>,
    pub imap_host: Option<String>,
    pub imap_port: Option<u16>,
    pub imap_encryption: Option<Encryption>,
    pub smtp_host: Option<String>,
    pub smtp_port: Option<u16>,
    pub smtp_encryption: Option<Encryption>,
    pub username: Option<String>,
    pub password: Option<String>,
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_encryption_from_str_lossy() {
        assert!(matches!(Encryption::from_str_lossy("Tls"), Encryption::Tls));
        assert!(matches!(
            Encryption::from_str_lossy("StartTls"),
            Encryption::StartTls
        ));
        assert!(matches!(
            Encryption::from_str_lossy("None"),
            Encryption::None
        ));
        assert!(matches!(
            Encryption::from_str_lossy("unknown"),
            Encryption::None
        ));
    }

    #[test]
    fn test_account_serialization_round_trip() {
        let account = Account {
            id: "acc1".to_string(),
            email: "test@example.com".to_string(),
            display_name: "Test User".to_string(),
            auth_type: "password".to_string(),
            imap_host: "imap.example.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.example.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "test@example.com".to_string(),
            oauth2_client_id: None,
            oauth2_client_secret: None,
            oauth2_refresh_token: None,
            oauth2_access_token: None,
            oauth2_expires_at: None,
            password: None,
            created_at: "2024-01-01T00:00:00Z".to_string(),
            updated_at: "2024-01-01T00:00:00Z".to_string(),
        };

        let json = serde_json::to_string(&account).unwrap();
        let decoded: Account = serde_json::from_str(&json).unwrap();

        assert_eq!(decoded.id, account.id);
        assert_eq!(decoded.email, account.email);
        assert_eq!(decoded.imap_encryption.as_str(), "tls");
        assert_eq!(decoded.smtp_encryption.as_str(), "starttls");
    }

    #[test]
    fn test_account_secrets_not_serialized() {
        let account = Account {
            id: "acc1".to_string(),
            email: "test@example.com".to_string(),
            display_name: "Test User".to_string(),
            auth_type: "password".to_string(),
            imap_host: "imap.example.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.example.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "test@example.com".to_string(),
            oauth2_client_id: Some("client_id".to_string()),
            oauth2_client_secret: Some("client_secret".to_string()),
            oauth2_refresh_token: Some("refresh_token".to_string()),
            oauth2_access_token: Some("access_token".to_string()),
            oauth2_expires_at: None,
            password: Some("password123".to_string()),
            created_at: "2024-01-01T00:00:00Z".to_string(),
            updated_at: "2024-01-01T00:00:00Z".to_string(),
        };

        let json = serde_json::to_string(&account).unwrap();

        // Secrets should NOT be in the serialized JSON
        assert!(
            !json.contains("client_secret"),
            "oauth2_client_secret should not be serialized"
        );
        assert!(
            !json.contains("refresh_token"),
            "oauth2_refresh_token should not be serialized"
        );
        assert!(
            !json.contains("access_token"),
            "oauth2_access_token should not be serialized"
        );
        assert!(
            !json.contains("password123"),
            "password should not be serialized"
        );

        // Non-secret fields should be present
        assert!(json.contains("acc1"), "id should be serialized");
        assert!(
            json.contains("test@example.com"),
            "email should be serialized"
        );
        assert!(
            json.contains("client_id"),
            "oauth2_client_id should be serialized"
        );
    }
}

/// Lightweight account info for list views.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AccountSummary {
    pub id: String,
    pub email: String,
    pub display_name: String,
    pub auth_type: String,
}

/// Safe account response for IPC/frontend consumption.
/// Explicitly excludes all secret fields (password, oauth2 tokens).
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AccountResponse {
    pub id: String,
    pub email: String,
    pub display_name: String,
    pub auth_type: String,
    pub imap_host: String,
    pub imap_port: u16,
    pub imap_encryption: Encryption,
    pub smtp_host: String,
    pub smtp_port: u16,
    pub smtp_encryption: Encryption,
    pub username: String,
    pub oauth2_client_id: Option<String>,
    pub created_at: String,
    pub updated_at: String,
}

impl AccountResponse {
    /// Create a safe response from the internal Account model.
    /// Secrets are explicitly excluded.
    pub fn from_account(account: &Account) -> Self {
        Self {
            id: account.id.clone(),
            email: account.email.clone(),
            display_name: account.display_name.clone(),
            auth_type: account.auth_type.clone(),
            imap_host: account.imap_host.clone(),
            imap_port: account.imap_port,
            imap_encryption: account.imap_encryption.clone(),
            smtp_host: account.smtp_host.clone(),
            smtp_port: account.smtp_port,
            smtp_encryption: account.smtp_encryption.clone(),
            username: account.username.clone(),
            oauth2_client_id: account.oauth2_client_id.clone(),
            created_at: account.created_at.clone(),
            updated_at: account.updated_at.clone(),
        }
    }
}

#[cfg(test)]
mod account_response_tests {
    use super::*;

    #[test]
    fn test_account_response_excludes_secrets() {
        // Create an internal account with all secrets populated
        let internal_account = Account {
            id: "acc1".to_string(),
            email: "test@example.com".to_string(),
            display_name: "Test User".to_string(),
            auth_type: "oauth2".to_string(),
            imap_host: "imap.example.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.example.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "test@example.com".to_string(),
            oauth2_client_id: Some("client_id".to_string()),
            oauth2_client_secret: Some("client_secret".to_string()),
            oauth2_refresh_token: Some("refresh_token".to_string()),
            oauth2_access_token: Some("access_token".to_string()),
            oauth2_expires_at: None,
            password: Some("password123".to_string()),
            created_at: "2024-01-01T00:00:00Z".to_string(),
            updated_at: "2024-01-01T00:00:00Z".to_string(),
        };

        // Convert to response DTO
        let response = AccountResponse::from_account(&internal_account);

        // Serialize the response
        let json = serde_json::to_string(&response).unwrap();

        // Verify secrets are NOT in the response JSON
        assert!(
            !json.contains("client_secret"),
            "oauth2_client_secret should not be in response"
        );
        assert!(
            !json.contains("refresh_token"),
            "oauth2_refresh_token should not be in response"
        );
        assert!(
            !json.contains("access_token"),
            "oauth2_access_token should not be in response"
        );
        assert!(
            !json.contains("password"),
            "password field should not be in response"
        );
        assert!(
            !json.contains("password123"),
            "password value should not be in response"
        );
        assert!(!json.contains("secret"), "no secrets should be in response");

        // Verify non-secret fields ARE present
        assert!(json.contains("acc1"), "id should be in response");
        assert!(
            json.contains("test@example.com"),
            "email should be in response"
        );
        assert!(
            json.contains("Test User"),
            "display_name should be in response"
        );
        assert!(json.contains("oauth2"), "auth_type should be in response");
        assert!(
            json.contains("imap.example.com"),
            "imap_host should be in response"
        );
        assert!(
            json.contains("client_id"),
            "oauth2_client_id should be in response"
        );
    }

    #[test]
    fn test_account_response_has_no_secret_fields() {
        // This test ensures the AccountResponse struct definition itself
        // doesn't accidentally include secret fields
        let response = AccountResponse {
            id: "acc1".to_string(),
            email: "test@example.com".to_string(),
            display_name: "Test User".to_string(),
            auth_type: "oauth2".to_string(), // use oauth2 to avoid "password" in auth_type
            imap_host: "imap.example.com".to_string(),
            imap_port: 993,
            imap_encryption: Encryption::Tls,
            smtp_host: "smtp.example.com".to_string(),
            smtp_port: 587,
            smtp_encryption: Encryption::StartTls,
            username: "test@example.com".to_string(),
            oauth2_client_id: None,
            created_at: "2024-01-01T00:00:00Z".to_string(),
            updated_at: "2024-01-01T00:00:00Z".to_string(),
        };

        // Just verify it serializes without secrets
        let json = serde_json::to_string(&response).unwrap();
        assert!(json.contains("acc1"));
        // Check for actual secret field names, not the word "password" which appears in auth_type
        assert!(
            !json.contains("\"password\":"),
            "password field should not exist"
        );
        assert!(!json.contains("access_token"));
        assert!(!json.contains("refresh_token"));
        assert!(!json.contains("client_secret"));
    }
}
