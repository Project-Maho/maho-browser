use sea_orm::entity::prelude::*;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Serialize, Deserialize)]
#[sea_orm(table_name = "room_meta")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub room_id: String,
    pub next_seq: i64,
    pub last_activity: i64,
    pub total_bytes: i64,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
