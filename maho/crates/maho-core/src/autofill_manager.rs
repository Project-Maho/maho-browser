use maho_types::autofill::{AutofillAddress, AutofillPayment};

pub struct AutofillManager {
    addresses: Vec<AutofillAddress>,
    payments: Vec<AutofillPayment>,
}

impl AutofillManager {
    pub fn new() -> Self {
        Self {
            addresses: Vec::new(),
            payments: Vec::new(),
        }
    }

    pub fn list_addresses(&self) -> &[AutofillAddress] {
        &self.addresses
    }

    pub fn list_payments(&self) -> &[AutofillPayment] {
        &self.payments
    }

    pub fn add_address(&mut self, address: AutofillAddress) -> &AutofillAddress {
        self.addresses.push(address);
        self.addresses.last().unwrap()
    }

    pub fn delete_address(&mut self, id: &str) -> bool {
        let len_before = self.addresses.len();
        self.addresses.retain(|a| a.id != id);
        self.addresses.len() < len_before
    }

    pub fn add_payment(&mut self, payment: AutofillPayment) -> &AutofillPayment {
        self.payments.push(payment);
        self.payments.last().unwrap()
    }

    pub fn delete_payment(&mut self, id: &str) -> bool {
        let len_before = self.payments.len();
        self.payments.retain(|p| p.id != id);
        self.payments.len() < len_before
    }
}

impl Default for AutofillManager {
    fn default() -> Self {
        Self::new()
    }
}
