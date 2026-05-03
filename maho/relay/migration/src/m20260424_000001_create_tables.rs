use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum Messages {
    Table,
    Id,
    RoomId,
    Seq,
    Blob,
    CreatedAt,
}

#[derive(DeriveIden)]
enum RoomMeta {
    Table,
    RoomId,
    NextSeq,
    LastActivity,
    TotalBytes,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(Messages::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Messages::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Messages::RoomId).string().not_null())
                    .col(ColumnDef::new(Messages::Seq).big_integer().not_null())
                    .col(ColumnDef::new(Messages::Blob).blob().not_null())
                    .col(ColumnDef::new(Messages::CreatedAt).big_integer().not_null())
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_messages_room_seq")
                    .table(Messages::Table)
                    .col(Messages::RoomId)
                    .col(Messages::Seq)
                    .unique()
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(RoomMeta::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(RoomMeta::RoomId)
                            .string()
                            .not_null()
                            .primary_key(),
                    )
                    .col(
                        ColumnDef::new(RoomMeta::NextSeq)
                            .big_integer()
                            .not_null()
                            .default(0),
                    )
                    .col(ColumnDef::new(RoomMeta::LastActivity).big_integer().not_null())
                    .col(
                        ColumnDef::new(RoomMeta::TotalBytes)
                            .big_integer()
                            .not_null()
                            .default(0),
                    )
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(Messages::Table).to_owned())
            .await?;
        manager
            .drop_table(Table::drop().table(RoomMeta::Table).to_owned())
            .await?;
        Ok(())
    }
}
