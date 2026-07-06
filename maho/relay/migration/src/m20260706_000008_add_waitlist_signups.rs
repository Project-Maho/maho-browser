use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum WaitlistSignups {
    Table,
    Id,
    Email,
    Source,
    IpHash,
    CreatedAt,
    Metadata,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(WaitlistSignups::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(WaitlistSignups::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(
                        ColumnDef::new(WaitlistSignups::Email)
                            .text()
                            .not_null(),
                    )
                    .col(
                        ColumnDef::new(WaitlistSignups::Source)
                            .text()
                            .not_null()
                            .default("general"),
                    )
                    .col(
                        ColumnDef::new(WaitlistSignups::IpHash)
                            .text()
                            .not_null(),
                    )
                    .col(
                        ColumnDef::new(WaitlistSignups::CreatedAt)
                            .big_integer()
                            .not_null(),
                    )
                    .col(
                        ColumnDef::new(WaitlistSignups::Metadata)
                            .text()
                            .null(),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .name("idx_waitlist_email_source")
                    .table(WaitlistSignups::Table)
                    .col(WaitlistSignups::Email)
                    .col(WaitlistSignups::Source)
                    .unique()
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .name("idx_waitlist_created_at")
                    .table(WaitlistSignups::Table)
                    .col(WaitlistSignups::CreatedAt)
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(
                Table::drop()
                    .table(WaitlistSignups::Table)
                    .to_owned(),
            )
            .await?;

        Ok(())
    }
}
