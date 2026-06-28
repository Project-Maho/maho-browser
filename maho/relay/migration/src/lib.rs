pub use sea_orm_migration::prelude::*;

mod m20260424_000001_create_tables;
mod m20260502_000002_create_auth_tables;
mod m20260627_000003_add_billing;
mod m20260627_000004_add_email_verification;
mod m20260628_000005_add_consumed_bypass_tokens;
mod m20260628_000006_add_payg_credits;

pub struct Migrator;

#[async_trait::async_trait]
impl MigratorTrait for Migrator {
    fn migrations() -> Vec<Box<dyn MigrationTrait>> {
        vec![
            Box::new(m20260424_000001_create_tables::Migration),
            Box::new(m20260502_000002_create_auth_tables::Migration),
            Box::new(m20260627_000003_add_billing::Migration),
            Box::new(m20260627_000004_add_email_verification::Migration),
            Box::new(m20260628_000005_add_consumed_bypass_tokens::Migration),
            Box::new(m20260628_000006_add_payg_credits::Migration),
        ]
    }
}
