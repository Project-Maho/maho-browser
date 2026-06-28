use std::path::PathBuf;
use std::process::ExitCode;

use base64::Engine;
use sha2::{Digest, Sha256};

use maho_relay::updates::releases::{Platform, ReleaseManifest, ReleaseStore};
use maho_relay::updates::signer::UpdateSigner;

fn print_usage() {
    eprintln!("Usage:");
    eprintln!("  maho-update-tool keygen --private-out PATH --public-out PATH");
    eprintln!("  maho-update-tool sign-release \\");
    eprintln!("      --private-key PATH \\");
    eprintln!("      --releases-dir PATH \\");
    eprintln!("      --platform [macos|windows|linux] \\");
    eprintln!("      --channel [stable|beta|dev|canary] \\");
    eprintln!("      --version 1.2.3 \\");
    eprintln!("      --binary PATH \\");
    eprintln!("      --signer-cn \"Maho Browser Inc.\" \\");
    eprintln!("      [--rollout-bucket 100] \\");
    eprintln!("      [--release-notes-url URL]");
    eprintln!("  maho-update-tool show-pubkey --private-key PATH");
}

#[tokio::main]
async fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        print_usage();
        return ExitCode::from(2);
    }
    let result = match args[1].as_str() {
        "keygen" => keygen(&args[2..]).await,
        "sign-release" => sign_release(&args[2..]).await,
        "show-pubkey" => show_pubkey(&args[2..]).await,
        "--help" | "-h" | "help" => {
            print_usage();
            return ExitCode::SUCCESS;
        }
        other => {
            eprintln!("unknown subcommand: {other}");
            print_usage();
            return ExitCode::from(2);
        }
    };
    match result {
        Ok(()) => ExitCode::SUCCESS,
        Err(e) => {
            eprintln!("error: {e}");
            ExitCode::FAILURE
        }
    }
}

fn flag<'a>(args: &'a [String], name: &str) -> Option<&'a str> {
    let mut iter = args.iter();
    while let Some(a) = iter.next() {
        if a == name {
            return iter.next().map(String::as_str);
        }
    }
    None
}

fn require_flag<'a>(args: &'a [String], name: &str) -> Result<&'a str, Box<dyn std::error::Error>> {
    flag(args, name).ok_or_else(|| format!("missing required flag {name}").into())
}

async fn keygen(args: &[String]) -> Result<(), Box<dyn std::error::Error>> {
    let priv_out = PathBuf::from(require_flag(args, "--private-out")?);
    let pub_out = PathBuf::from(require_flag(args, "--public-out")?);

    let (signer, seed) = UpdateSigner::generate();
    let pubkey = signer.public_key_bytes();

    if let Some(parent) = priv_out.parent() {
        tokio::fs::create_dir_all(parent).await?;
    }
    if let Some(parent) = pub_out.parent() {
        tokio::fs::create_dir_all(parent).await?;
    }

    tokio::fs::write(&priv_out, seed).await?;
    tokio::fs::write(&pub_out, pubkey).await?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut perms = tokio::fs::metadata(&priv_out).await?.permissions();
        perms.set_mode(0o600);
        tokio::fs::set_permissions(&priv_out, perms).await?;
    }

    println!("private key (DO NOT SHIP): {}", priv_out.display());
    println!("public key (browser side): {}", pub_out.display());
    println!();
    println!("Sparkle SUPublicEDKey (base64, paste into Info.plist):");
    println!("  {}", base64::engine::general_purpose::STANDARD.encode(pubkey));
    Ok(())
}

async fn show_pubkey(args: &[String]) -> Result<(), Box<dyn std::error::Error>> {
    let path = PathBuf::from(require_flag(args, "--private-key")?);
    let signer = UpdateSigner::from_path(&path).await?;
    let pubkey = signer.public_key_bytes();
    println!("public key hex:    {}", hex::encode(pubkey));
    println!(
        "public key base64: {}",
        base64::engine::general_purpose::STANDARD.encode(pubkey)
    );
    Ok(())
}

async fn sign_release(args: &[String]) -> Result<(), Box<dyn std::error::Error>> {
    let priv_path = PathBuf::from(require_flag(args, "--private-key")?);
    let releases_dir = PathBuf::from(require_flag(args, "--releases-dir")?);
    let platform_str = require_flag(args, "--platform")?;
    let channel = require_flag(args, "--channel")?;
    let version = require_flag(args, "--version")?;
    let binary_path = PathBuf::from(require_flag(args, "--binary")?);
    let signer_cn = require_flag(args, "--signer-cn")?;
    let rollout_bucket: u32 = flag(args, "--rollout-bucket")
        .map(str::parse)
        .transpose()?
        .unwrap_or(100);
    let release_notes_url = flag(args, "--release-notes-url").map(ToString::to_string);

    let platform = Platform::parse(platform_str)
        .ok_or_else(|| format!("invalid platform: {platform_str}"))?;

    if !maho_relay::updates::releases::is_valid_channel(channel) {
        return Err(format!("invalid channel: {channel}").into());
    }
    if rollout_bucket > 100 {
        return Err(format!("rollout-bucket must be 0..=100, got {rollout_bucket}").into());
    }

    let signer = UpdateSigner::from_path(&priv_path).await?;

    let binary_bytes = tokio::fs::read(&binary_path).await?;
    let binary_size = binary_bytes.len() as u64;
    let mut hasher = Sha256::new();
    hasher.update(&binary_bytes);
    let binary_sha256 = hex::encode(hasher.finalize());

    let binary_filename = binary_path
        .file_name()
        .and_then(|n| n.to_str())
        .ok_or("binary path has no valid filename")?
        .to_string();

    let binary_ed25519_signature_base64 = if matches!(platform, Platform::Macos) {
        Some(signer.sign_base64(&binary_bytes))
    } else {
        None
    };

    let release_date = chrono::Utc::now().to_rfc2822();

    let manifest = ReleaseManifest {
        version: version.to_string(),
        channel: channel.to_string(),
        binary_filename: binary_filename.clone(),
        binary_sha256,
        binary_size,
        installer_signer_cn: signer_cn.to_string(),
        rollout_bucket,
        release_date,
        release_notes_url,
        binary_ed25519_signature_base64,
    };

    let store = ReleaseStore::new(&releases_dir);
    let dest = store.binary_path(platform, channel, &binary_filename);
    if let Some(parent) = dest.parent() {
        tokio::fs::create_dir_all(parent).await?;
    }
    tokio::fs::copy(&binary_path, &dest).await?;
    store.save(platform, channel, &manifest).await?;

    println!(
        "signed {} {} {} → {}",
        platform_str,
        channel,
        version,
        store.manifest_path(platform, channel).display()
    );
    println!("  binary: {}", dest.display());
    println!("  sha256: {}", manifest.binary_sha256);
    if let Some(sig) = &manifest.binary_ed25519_signature_base64 {
        println!("  sparkle:edSignature: {sig}");
    }
    Ok(())
}
