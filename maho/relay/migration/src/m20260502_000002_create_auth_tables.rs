use sea_orm_migration::prelude::*;

#[derive(DeriveMigrationName)]
pub struct Migration;

#[derive(DeriveIden)]
enum Users {
    Table,
    Id,
    Email,
    PasswordHash,
    DisplayName,
    CreatedAt,
    LockedUntil,
    FailedLoginAttempts,
    LastFailedLoginAt,
}

#[derive(DeriveIden)]
enum Devices {
    Table,
    Id,
    UserId,
    Name,
    DeviceType,
    RoomId,
    CreatedAt,
    LastSeen,
}

#[derive(DeriveIden)]
enum Sessions {
    Table,
    Id,
    UserId,
    DeviceId,
    TokenHash,
    RefreshHash,
    ExpiresAt,
    RefreshExpiresAt,
    LastUsedAt,
    CreatedAt,
}

#[async_trait::async_trait]
impl MigrationTrait for Migration {
    async fn up(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .create_table(
                Table::create()
                    .table(Users::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Users::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Users::Email).string().not_null())
                    .col(ColumnDef::new(Users::PasswordHash).string().not_null())
                    .col(ColumnDef::new(Users::DisplayName).string().not_null())
                    .col(ColumnDef::new(Users::CreatedAt).big_integer().not_null())
                    .col(ColumnDef::new(Users::LockedUntil).big_integer().null())
                    .col(
                        ColumnDef::new(Users::FailedLoginAttempts)
                            .integer()
                            .not_null()
                            .default(0),
                    )
                    .col(ColumnDef::new(Users::LastFailedLoginAt).big_integer().null())
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_users_email")
                    .table(Users::Table)
                    .col(Users::Email)
                    .unique()
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(Devices::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Devices::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Devices::UserId).integer().not_null())
                    .col(ColumnDef::new(Devices::Name).string().not_null())
                    .col(ColumnDef::new(Devices::DeviceType).string().not_null())
                    .col(ColumnDef::new(Devices::RoomId).string().not_null())
                    .col(ColumnDef::new(Devices::CreatedAt).big_integer().not_null())
                    .col(ColumnDef::new(Devices::LastSeen).big_integer().null())
                    .foreign_key(
                        ForeignKey::create()
                            .name("fk_devices_user_id")
                            .from(Devices::Table, Devices::UserId)
                            .to(Users::Table, Users::Id)
                            .on_delete(ForeignKeyAction::Cascade)
                            .on_update(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_devices_user_id")
                    .table(Devices::Table)
                    .col(Devices::UserId)
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_devices_room_id")
                    .table(Devices::Table)
                    .col(Devices::RoomId)
                    .unique()
                    .to_owned(),
            )
            .await?;

        manager
            .create_table(
                Table::create()
                    .table(Sessions::Table)
                    .if_not_exists()
                    .col(
                        ColumnDef::new(Sessions::Id)
                            .integer()
                            .not_null()
                            .auto_increment()
                            .primary_key(),
                    )
                    .col(ColumnDef::new(Sessions::UserId).integer().not_null())
                    .col(ColumnDef::new(Sessions::DeviceId).integer().not_null())
                    .col(ColumnDef::new(Sessions::TokenHash).binary().not_null())
                    .col(ColumnDef::new(Sessions::RefreshHash).binary().not_null())
                    .col(ColumnDef::new(Sessions::ExpiresAt).big_integer().not_null())
                    .col(ColumnDef::new(Sessions::RefreshExpiresAt).big_integer().not_null())
                    .col(ColumnDef::new(Sessions::LastUsedAt).big_integer().null())
                    .col(ColumnDef::new(Sessions::CreatedAt).big_integer().not_null())
                    .foreign_key(
                        ForeignKey::create()
                            .name("fk_sessions_user_id")
                            .from(Sessions::Table, Sessions::UserId)
                            .to(Users::Table, Users::Id)
                            .on_delete(ForeignKeyAction::Cascade)
                            .on_update(ForeignKeyAction::Cascade),
                    )
                    .foreign_key(
                        ForeignKey::create()
                            .name("fk_sessions_device_id")
                            .from(Sessions::Table, Sessions::DeviceId)
                            .to(Devices::Table, Devices::Id)
                            .on_delete(ForeignKeyAction::Cascade)
                            .on_update(ForeignKeyAction::Cascade),
                    )
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_sessions_user_id")
                    .table(Sessions::Table)
                    .col(Sessions::UserId)
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_sessions_device_id")
                    .table(Sessions::Table)
                    .col(Sessions::DeviceId)
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_sessions_token_hash")
                    .table(Sessions::Table)
                    .col(Sessions::TokenHash)
                    .unique()
                    .to_owned(),
            )
            .await?;

        manager
            .create_index(
                Index::create()
                    .if_not_exists()
                    .name("idx_sessions_refresh_hash")
                    .table(Sessions::Table)
                    .col(Sessions::RefreshHash)
                    .unique()
                    .to_owned(),
            )
            .await?;

        Ok(())
    }

    async fn down(&self, manager: &SchemaManager) -> Result<(), DbErr> {
        manager
            .drop_table(Table::drop().table(Sessions::Table).to_owned())
            .await?;
        manager
            .drop_table(Table::drop().table(Devices::Table).to_owned())
            .await?;
        manager
            .drop_table(Table::drop().table(Users::Table).to_owned())
            .await?;
        Ok(())
    }
}
