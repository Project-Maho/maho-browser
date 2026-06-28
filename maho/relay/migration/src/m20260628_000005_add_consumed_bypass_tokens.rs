use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum ConsumedBypassTokens {
    Table,
    TokenHash,
    UserId,
    ConsumedAt,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(ConsumedBypassTokens::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(ConsumedBypassTokens::TokenHash)
                            .blob()
                            .not_null()
                            .primary_key(),
                    )
                    .col(
                        ColumnDef::new(ConsumedBypassTokens::UserId)
                            .integer()
                            .not_null(),
                    )
                    .col(
                        ColumnDef::new(ConsumedBypassTokens::ConsumedAt)
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
            .drop_table(
                Table::drop()
                    .table(ConsumedBypassTokens::Table)
                    .to_owned(),
            )
            .await?;

        Ok(())
    }
}
