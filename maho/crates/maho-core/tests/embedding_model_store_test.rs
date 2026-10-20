use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};
use std::net::TcpListener;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::{Arc, Mutex};

use maho_core::embedding_model_store::{
    ensure_model, model_dir, multilingual_e5_small, verified_dir, ModelFile, ModelManifest,
};
use maho_core::error::CoreError;
use sha2::{Digest, Sha256};

type Bodies = Arc<Mutex<HashMap<String, Vec<Vec<u8>>>>>;

struct Fixture {
    base_url: String,
    bodies: Bodies,
    hits: Arc<AtomicUsize>,
}

fn serve() -> Fixture {
    let listener = TcpListener::bind("127.0.0.1:0").unwrap();
    let base_url = format!("http://{}", listener.local_addr().unwrap());
    let bodies: Bodies = Arc::default();
    let hits = Arc::new(AtomicUsize::new(0));
    let (b, h) = (Arc::clone(&bodies), Arc::clone(&hits));
    std::thread::spawn(move || {
        for stream in listener.incoming() {
            let Ok(mut stream) = stream else { continue };
            let mut reader = BufReader::new(stream.try_clone().unwrap());
            let mut request = String::new();
            reader.read_line(&mut request).unwrap();
            loop {
                let mut line = String::new();
                if reader.read_line(&mut line).unwrap() == 0 || line == "\r\n" {
                    break;
                }
            }
            h.fetch_add(1, Ordering::SeqCst);
            let path = request.split_whitespace().nth(1).unwrap_or("/").to_string();
            let body = {
                let mut map = b.lock().unwrap();
                map.get_mut(&path).and_then(|queue| {
                    if queue.len() > 1 {
                        Some(queue.remove(0))
                    } else {
                        queue.first().cloned()
                    }
                })
            };
            let response = match body {
                Some(body) => {
                    let mut r = format!(
                        "HTTP/1.1 200 OK\r\nContent-Length: {}\r\nConnection: close\r\n\r\n",
                        body.len()
                    )
                    .into_bytes();
                    r.extend(body);
                    r
                }
                None => b"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n"
                    .to_vec(),
            };
            let _ = stream.write_all(&response);
        }
    });
    Fixture {
        base_url,
        bodies,
        hits,
    }
}

fn sha(bytes: &[u8]) -> &'static str {
    let digest = Sha256::digest(bytes);
    Box::leak(
        digest
            .iter()
            .map(|b| format!("{b:02x}"))
            .collect::<String>()
            .into_boxed_str(),
    )
}

fn manifest(fixture: &Fixture, files: &[(&'static str, &[u8])]) -> ModelManifest {
    ModelManifest {
        dir_name: "test-model",
        base_url: fixture.base_url.clone(),
        files: files
            .iter()
            .map(|(name, body)| ModelFile {
                name,
                size: body.len() as u64,
                sha256: sha(body),
            })
            .collect(),
    }
}

fn runtime() -> tokio::runtime::Runtime {
    tokio::runtime::Builder::new_current_thread()
        .enable_all()
        .build()
        .unwrap()
}

#[test]
fn fetch_verifies_and_caches_under_profile_models() {
    let fixture = serve();
    let config = b"{\"hidden_size\":384}".to_vec();
    let weights = vec![7u8; 4096];
    fixture.bodies.lock().unwrap().extend([
        ("/config.json".to_string(), vec![config.clone()]),
        ("/model.safetensors".to_string(), vec![weights.clone()]),
    ]);
    let manifest = manifest(
        &fixture,
        &[("config.json", &config), ("model.safetensors", &weights)],
    );
    let profile = tempfile::tempdir().unwrap();
    let client = reqwest::Client::new();

    let dir = runtime()
        .block_on(ensure_model(&client, profile.path(), &manifest))
        .unwrap();
    assert_eq!(dir, profile.path().join("models").join("test-model"));
    assert_eq!(
        std::fs::read(dir.join("model.safetensors")).unwrap(),
        weights
    );
    assert_eq!(verified_dir(profile.path(), &manifest), Some(dir.clone()));
    assert!(std::fs::read_dir(&dir).unwrap().all(|e| !e
        .unwrap()
        .file_name()
        .to_string_lossy()
        .ends_with(".part")));

    let hits = fixture.hits.load(Ordering::SeqCst);
    runtime()
        .block_on(ensure_model(&client, profile.path(), &manifest))
        .unwrap();
    assert_eq!(
        fixture.hits.load(Ordering::SeqCst),
        hits,
        "cached files were downloaded again"
    );
}

#[test]
fn sha_mismatch_is_rejected_and_not_cached() {
    let fixture = serve();
    let expected = vec![1u8; 2048];
    let tampered = vec![2u8; 2048];
    fixture
        .bodies
        .lock()
        .unwrap()
        .insert("/model.safetensors".to_string(), vec![tampered]);
    let manifest = manifest(&fixture, &[("model.safetensors", &expected)]);
    let profile = tempfile::tempdir().unwrap();

    let error = runtime()
        .block_on(ensure_model(
            &reqwest::Client::new(),
            profile.path(),
            &manifest,
        ))
        .unwrap_err();
    assert!(matches!(error, CoreError::Embedding(_)), "{error}");
    let dir = model_dir(profile.path(), &manifest);
    assert_eq!(
        std::fs::read_dir(&dir).unwrap().count(),
        0,
        "a bad file was cached"
    );
    assert_eq!(
        fixture.hits.load(Ordering::SeqCst),
        2,
        "a corrupt download is retried once"
    );
    assert_eq!(verified_dir(profile.path(), &manifest), None);
}

#[test]
fn corrupt_cached_file_is_replaced_by_one_retry() {
    let fixture = serve();
    let good = vec![3u8; 1024];
    fixture.bodies.lock().unwrap().insert(
        "/model.safetensors".to_string(),
        vec![vec![9u8; 1024], good.clone()],
    );
    let manifest = manifest(&fixture, &[("model.safetensors", &good)]);
    let profile = tempfile::tempdir().unwrap();
    let dir = model_dir(profile.path(), &manifest);
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("model.safetensors"), b"stale").unwrap();

    runtime()
        .block_on(ensure_model(
            &reqwest::Client::new(),
            profile.path(),
            &manifest,
        ))
        .unwrap();
    assert_eq!(std::fs::read(dir.join("model.safetensors")).unwrap(), good);
}

#[test]
fn offline_is_typed_unavailable() {
    let unreachable = ModelManifest {
        dir_name: "offline",
        base_url: "http://127.0.0.1:9".to_string(),
        files: vec![ModelFile {
            name: "model.safetensors",
            size: 1,
            sha256: "00",
        }],
    };
    let profile = tempfile::tempdir().unwrap();
    let error = runtime()
        .block_on(ensure_model(
            &reqwest::Client::new(),
            profile.path(),
            &unreachable,
        ))
        .unwrap_err();
    assert!(
        matches!(error, CoreError::EmbeddingUnavailable(_)),
        "{error}"
    );
}

#[test]
fn default_manifest_pins_revision_sizes_and_hashes() {
    let manifest = multilingual_e5_small();
    assert!(manifest
        .base_url
        .contains("614241f622f53c4eeff9890bdc4f31cfecc418b3"));
    assert_eq!(manifest.files.len(), 3);
    for file in &manifest.files {
        assert_eq!(file.sha256.len(), 64);
        assert!(file.size > 0);
    }
}
