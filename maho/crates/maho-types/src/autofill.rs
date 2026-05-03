use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillAddress {
    pub id: String,
    pub name: String,
    pub street: String,
    pub city: String,
    pub state: String,
    pub zip: String,
    pub country: String,
    pub phone: Option<String>,
    pub email: Option<String>,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillPayment {
    pub id: String,
    pub card_name: String,
    pub last_four: String,
    pub expiry: String,
}

#[derive(Clone, Debug, Serialize, Deserialize, PartialEq, Default)]
#[serde(rename_all = "lowercase")]
pub enum PasswordAutofillProvider {
    #[default]
    Builtin,
    System,
    Disabled,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AutofillSettings {
    pub addresses_enabled: bool,
    pub payments_enabled: bool,
    #[serde(default)]
    pub password_provider: PasswordAutofillProvider,
}

impl Default for AutofillSettings {
    fn default() -> Self {
        Self {
            addresses_enabled: true,
            payments_enabled: true,
            password_provider: PasswordAutofillProvider::default(),
        }
    }
}

#[derive(Clone, Debug, Serialize, Deserialize, Default)]
#[serde(rename_all = "camelCase")]
pub struct AutofillSettingsUpdate {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub addresses_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub payments_enabled: Option<bool>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub password_provider: Option<PasswordAutofillProvider>,
}
