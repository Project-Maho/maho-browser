use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum Users {
    Table,
    Tier,
    SubscriptionStatus,
    EmailVerified,
    SubscriptionExpiresAt,
    PeriodStartAt,
    AnniversaryResetDay,
}

#[derive(DeriveIden)]
enum ProcessedEvents {
    Table,
    EventId,
    ProcessedAt,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        // Alter users table to add billing columns sequentially (SQLite compatibility)
        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::Tier)
                            .string()
                            .not_null()
                            .default("free"),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::SubscriptionStatus)
                            .string()
                            .not_null()
                            .default("none"),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::EmailVerified)
                            .boolean()
                            .not_null()
                            .default(false),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::SubscriptionExpiresAt)
                            .big_integer()
                            .null(),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::PeriodStartAt)
                            .big_integer()
                            .null(),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .alter_table(
                Table::alter()
                    .table(Users::Table)
                    .add_column(
                        ColumnDef::new(Users::AnniversaryResetDay)
                            .integer()
                            .null(),
                    )
                    .to_owned(),
            )
            .await?;

        // Create processed_events table
        manager
            .create_table(
                Table::create()
                    .table(ProcessedEvents::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(ProcessedEvents::EventId)
                            .string()
                            .not_null()
                            .primary_key(),
                    )
                    .col(
                        ColumnDef::new(ProcessedEvents::ProcessedAt)
                            .big_integer()
                            .not_null(),
                    )
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(ProcessedEvents::Table).to_owned())
            .await?;
            
        Ok(())
    }
}
