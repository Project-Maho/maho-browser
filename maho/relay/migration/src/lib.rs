pub use sea_orm_migration::prelude::*;

mod m20260424_000001_create_tables;
mod m20260502_000002_create_auth_tables;

pub struct Migrator;

#[async_trait::async_trait]
impl MigratorTrait for Migrator {
    fn migrations() -> Vec<Box<dyn MigrationTrait>> {
        vec![
            Box::new(m20260424_000001_create_tables::Migration),
            Box::new(m20260502_000002_create_auth_tables::Migration),
        ]
    }
}
