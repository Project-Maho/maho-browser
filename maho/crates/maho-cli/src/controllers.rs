use anyhow::{bail, Result};
use clap::Subcommand;
use serde::Deserialize;

use crate::browser;

#[derive(Debug, Subcommand)]
pub enum ControllersCommands {
    /// List locally paired controllers.
    List,
    /// Revoke a paired controller by its ID.
    Revoke { id: String },
}

#[derive(Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
struct Controller {
    controller_id: String,
    kind: String,
    name: String,
}

pub async fn run(
    command: ControllersCommands,
    socket_path: Option<&str>,
    json_output: bool,
) -> Result<()> {
    let client = browser::connect(socket_path).await?;
    match command {
        ControllersCommands::List => {
            let result = client
                .call("maho/controllers/list", serde_json::json!({}))
                .await?;
            let controllers: Vec<Controller> = serde_json::from_value(result)?;
            if json_output {
                let safe: Vec<_> = controllers
                    .iter()
                    .map(|controller| {
                        serde_json::json!({
                            "controllerId": controller.controller_id,
                            "kind": controller.kind,
                            "name": controller.name,
                        })
                    })
                    .collect();
                println!("{}", serde_json::to_string_pretty(&safe)?);
            } else {
                println!("{:<34} {:<20} NAME", "ID", "KIND");
                for controller in controllers {
                    println!(
                        "{:<34} {:<20} {}",
                        controller.controller_id, controller.kind, controller.name
                    );
                }
            }
        }
        ControllersCommands::Revoke { id } => {
            let result = client
                .call(
                    "maho/controllers/revoke",
                    serde_json::json!({ "controllerId": id }),
                )
                .await?;
            if result.get("revoked").and_then(serde_json::Value::as_bool) != Some(true) {
                bail!("controller was not revoked");
            }
            if json_output {
                println!("{}", serde_json::to_string_pretty(&result)?);
            } else {
                println!("Controller {id} revoked.");
            }
        }
    }
    Ok(())
}
