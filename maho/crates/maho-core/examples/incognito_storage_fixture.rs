//! Offline incognito storage fixture: thin CLI over `seed` and `verify`.
//!
//! Usage:
//!   incognito_storage_fixture seed   --profile-root DIR --manifest INV --receipt FILE
//!   incognito_storage_fixture verify --baseline DIR --final DIR --manifest INV --receipt FILE

mod incognito_storage_fixture_support;

use incognito_storage_fixture_support as support;
use std::process::ExitCode;

fn main() -> ExitCode {
    let mut argv = std::env::args();
    let _bin = argv.next();
    let subcommand = match argv.next() {
        Some(sub) => sub,
        None => {
            eprintln!("usage: incognito_storage_fixture <seed|verify> --flag value ...");
            return ExitCode::from(2);
        }
    };

    let args = match support::Args::parse(argv) {
        Ok(args) => args,
        Err(err) => {
            eprintln!("argument error: {err}");
            return ExitCode::from(2);
        }
    };

    let result = match subcommand.as_str() {
        "seed" => support::seed::run(&args),
        "verify" => support::verify::run(&args),
        other => Err(format!("unknown subcommand: {other}")),
    };

    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(err) => {
            eprintln!("error: {err}");
            ExitCode::FAILURE
        }
    }
}
