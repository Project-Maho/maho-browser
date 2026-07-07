use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum Snapshots {
    Table,
    RoomId,
    HlcTsCeiling,
    CreatedAt,
    CreatorDeviceId,
    Blob,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(Snapshots::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Snapshots::RoomId)
                            .string()
                            .not_null()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Snapshots::HlcTsCeiling).big_integer().not_null())
                    .col(ColumnDef::new(Snapshots::CreatedAt).big_integer().not_null())
                    .col(ColumnDef::new(Snapshots::CreatorDeviceId).big_integer().not_null())
                    .col(ColumnDef::new(Snapshots::Blob).blob().not_null())
                    .to_owned(),
            )
            .await?;
        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(Snapshots::Table).to_owned())
            .await?;
        Ok(())
    }
}
