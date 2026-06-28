use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum Credits {
    Table,
    UserId,
    BalanceUsd,
    LifetimePurchasedUsd,
    LastPurchaseAt,
    LastConsumptionAt,
    CreatedAt,
    UpdatedAt,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(Credits::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Credits::UserId)
                            .integer()
                            .not_null()
                            .primary_key(),
                    )
                    .col(
                        ColumnDef::new(Credits::BalanceUsd)
                            .double()
                            .not_null()
                            .default(0.0),
                    )
                    .col(
                        ColumnDef::new(Credits::LifetimePurchasedUsd)
                            .double()
                            .not_null()
                            .default(0.0),
                    )
                    .col(ColumnDef::new(Credits::LastPurchaseAt).big_integer().null())
                    .col(
                        ColumnDef::new(Credits::LastConsumptionAt)
                            .big_integer()
                            .null(),
                    )
                    .col(
                        ColumnDef::new(Credits::CreatedAt)
                            .big_integer()
                            .not_null(),
                    )
                    .col(
                        ColumnDef::new(Credits::UpdatedAt)
                            .big_integer()
                            .not_null(),
                    )
                    .foreign_key(
                        ForeignKey::create()
                            .from(Credits::Table, Credits::UserId)
                            .to(Alias::new("users"), Alias::new("id")),
                    )
                    .to_owned(),
            )
            .await?;

        // Partial index for active balances
        manager
            .get_connection()
            .execute_unprepared(
                "CREATE INDEX idx_credits_balance ON credits(balance_usd) WHERE balance_usd > 0",
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(Credits::Table).to_owned())
            .await?;

        Ok(())
    }
}
