use sea_orm::entity::prelude::*;
use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, PartialEq, DeriveEntityModel, Serialize, Deserialize)]
#[sea_orm(table_name = "consumed_bypass_tokens")]
pub struct Model {
    #[sea_orm(primary_key, auto_increment = false)]
    pub token_hash: Vec<u8>,
    pub user_id: i32,
    pub consumed_at: i64,
}

#[derive(Copy, Clone, Debug, EnumIter, DeriveRelation)]
pub enum Relation {}

impl ActiveModelBehavior for ActiveModel {}
