use std::collections::BTreeSet;
use std::io::Read;
use std::path::Path;

use base64::Engine;
use maho_agent::artifacts::{self, ArtifactHandle};
use maho_agent::tool_registry::{artifact_create_definition, get_platform_tools};

fn temp_root(test_name: &str) -> tempfile::TempDir {
    tempfile::Builder::new()
        .prefix(&format!("maho-artifact-{test_name}-"))
        .tempdir()
        .unwrap()
}

fn create(root: &Path, name: &str, json: &str) -> anyhow::Result<ArtifactHandle> {
    artifacts::create(root, name, "pptx", json.as_bytes())
}

const PNG_1X1: &str = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

fn entries(path: &Path) -> Vec<(String, Vec<u8>)> {
    let mut archive = zip::ZipArchive::new(std::fs::File::open(path).unwrap()).unwrap();
    (0..archive.len())
        .map(|i| {
            let mut file = archive.by_index(i).unwrap();
            let mut body = Vec::new();
            file.read_to_end(&mut body).unwrap();
            (file.name().to_string(), body)
        })
        .collect()
}

fn assert_well_formed(name: &str, body: &[u8]) {
    let mut reader = quick_xml::Reader::from_reader(body);
    reader.config_mut().check_end_names = true;
    let mut depth = 0i32;
    let mut buf = Vec::new();
    loop {
        match reader.read_event_into(&mut buf) {
            Ok(quick_xml::events::Event::Start(_)) => depth += 1,
            Ok(quick_xml::events::Event::End(_)) => depth -= 1,
            Ok(quick_xml::events::Event::Eof) => break,
            Ok(_) => {}
            Err(error) => panic!("{name} is not well-formed XML: {error}"),
        }
        buf.clear();
    }
    assert_eq!(depth, 0, "{name} has unbalanced elements");
}

fn attribute_values(body: &[u8], element: &[u8], attribute: &[u8]) -> Vec<String> {
    let mut reader = quick_xml::Reader::from_reader(body);
    let mut values = Vec::new();
    let mut buf = Vec::new();
    loop {
        match reader.read_event_into(&mut buf).unwrap() {
            quick_xml::events::Event::Empty(e) | quick_xml::events::Event::Start(e)
                if e.local_name().as_ref() == element =>
            {
                for attr in e.attributes().flatten() {
                    if attr.key.as_ref() == attribute {
                        values.push(String::from_utf8(attr.value.to_vec()).unwrap());
                    }
                }
            }
            quick_xml::events::Event::Eof => break,
            _ => {}
        }
        buf.clear();
    }
    values
}

fn resolve_target(rels_name: &str, target: &str) -> String {
    let source_dir = rels_name
        .trim_end_matches(".rels")
        .rsplit_once("/_rels/")
        .map(|(dir, _)| dir.to_string())
        .unwrap_or_default();
    let mut parts: Vec<&str> = source_dir.split('/').filter(|p| !p.is_empty()).collect();
    for segment in target.split('/') {
        match segment {
            ".." => {
                parts.pop();
            }
            "." | "" => {}
            other => parts.push(other),
        }
    }
    parts.join("/")
}

#[test]
fn pptx_is_a_complete_openxml_package_with_n_slides() {
    let root = temp_root("pptx-happy");
    let json = format!(
        r#"{{"slides":[
            {{"type":"title","title":"분기 보고서 & <요약>","subtitle":"2026 Q3"}},
            {{"type":"bullets","title":"Highlights","bullets":["Revenue +12%","Users \"doubled\"","\u0007bell"]}},
            {{"type":"image","title":"Chart","image_base64":"{PNG_1X1}"}}
        ]}}"#
    );
    let handle = create(root.path(), "deck.pptx", &json).unwrap();
    assert_eq!(
        handle.mime_type,
        "application/vnd.openxmlformats-officedocument.presentationml.presentation"
    );
    let path = artifacts::resolve(root.path(), &handle).unwrap();
    if let Some(dir) = std::env::var_os("MAHO_PPTX_EVIDENCE_DIR") {
        std::fs::copy(&path, Path::new(&dir).join("deck.pptx")).unwrap();
    }
    let parts = entries(&path);
    let names: BTreeSet<String> = parts.iter().map(|(n, _)| n.clone()).collect();

    for (name, body) in &parts {
        if name.ends_with(".xml") || name.ends_with(".rels") {
            assert_well_formed(name, body);
        }
    }
    for (name, body) in parts.iter().filter(|(n, _)| n.ends_with(".rels")) {
        for target in attribute_values(body, b"Relationship", b"Target") {
            let resolved = resolve_target(name, &target);
            assert!(
                names.contains(&resolved),
                "{name} points at missing part {resolved}"
            );
        }
    }

    let content_types = &parts
        .iter()
        .find(|(n, _)| n == "[Content_Types].xml")
        .unwrap()
        .1;
    let overrides: BTreeSet<String> = attribute_values(content_types, b"Override", b"PartName")
        .into_iter()
        .map(|p| p.trim_start_matches('/').to_string())
        .collect();
    let defaults: BTreeSet<String> = attribute_values(content_types, b"Default", b"Extension")
        .into_iter()
        .collect();
    for name in names.iter().filter(|n| *n != "[Content_Types].xml") {
        let extension = name.rsplit('.').next().unwrap();
        assert!(
            overrides.contains(name) || defaults.contains(extension),
            "{name} has no content type"
        );
    }

    let presentation = &parts
        .iter()
        .find(|(n, _)| n == "ppt/presentation.xml")
        .unwrap()
        .1;
    assert_eq!(attribute_values(presentation, b"sldId", b"id").len(), 3);
    assert!(names.contains("ppt/media/image3.png"));

    let slide1 = String::from_utf8(
        parts
            .iter()
            .find(|(n, _)| n == "ppt/slides/slide1.xml")
            .unwrap()
            .1
            .clone(),
    )
    .unwrap();
    assert!(slide1.contains("분기 보고서 &amp; &lt;요약&gt;"));
    let slide2 = String::from_utf8(
        parts
            .iter()
            .find(|(n, _)| n == "ppt/slides/slide2.xml")
            .unwrap()
            .1
            .clone(),
    )
    .unwrap();
    assert!(slide2.contains("Users &quot;doubled&quot;"));
    assert!(!slide2.contains('\u{7}'));
}

#[test]
fn pptx_rejects_bad_input_without_writing() {
    let root = temp_root("pptx-reject");
    let not_an_image = base64::engine::general_purpose::STANDARD.encode(b"GIF89a....");
    let cases = [
        ("a.pptx", r#"{"slides":[]}"#.to_string()),
        (
            "b.pptx",
            r#"{"slides":[{"type":"video","title":"x"}]}"#.to_string(),
        ),
        (
            "c.pptx",
            r#"{"slides":[{"type":"bullets","title":"x","bullets":[]}]}"#.to_string(),
        ),
        (
            "d.pptx",
            format!(r#"{{"slides":[{{"type":"image","image_base64":"{not_an_image}"}}]}}"#),
        ),
        (
            "e.pptx",
            r#"{"slides":[{"type":"image","image_base64":"!!!"}]}"#.to_string(),
        ),
        (
            "f.pptx",
            format!(
                r#"{{"slides":[{{"type":"title","title":"{}"}}]}}"#,
                "x".repeat(2_001)
            ),
        ),
    ];
    for (name, json) in cases {
        assert!(create(root.path(), name, &json).is_err(), "{name} accepted");
        assert!(!root.path().join(name).exists());
    }
    let too_many = format!(
        r#"{{"slides":[{}]}}"#,
        vec![r#"{"type":"title","title":"t"}"#; artifacts::MAX_PPTX_SLIDES + 1].join(",")
    );
    assert!(create(root.path(), "g.pptx", &too_many).is_err());
    assert!(create(
        root.path(),
        "../deck.pptx",
        r#"{"slides":[{"type":"title","title":"t"}]}"#
    )
    .is_err());
    assert!(create(
        root.path(),
        "deck.pdf",
        r#"{"slides":[{"type":"title","title":"t"}]}"#
    )
    .is_err());
}

#[test]
fn unknown_kind_is_rejected() {
    let root = temp_root("pptx-kind");
    let error = artifacts::create(root.path(), "x.key", "keynote", b"{}").unwrap_err();
    assert!(error.to_string().contains("unsupported artifact kind"));
}

#[test]
fn artifact_create_tool_is_registered_once_with_kind_enum() {
    let definition = artifact_create_definition();
    assert_eq!(definition.name, "artifact_create");
    let kinds: Vec<&str> = definition.parameters_schema["properties"]["kind"]["enum"]
        .as_array()
        .unwrap()
        .iter()
        .map(|v| v.as_str().unwrap())
        .collect();
    assert_eq!(kinds, ["xlsx", "docx", "pdf", "pptx"]);
    let tools = get_platform_tools();
    if cfg!(not(any(target_os = "ios", target_os = "android"))) {
        assert_eq!(
            tools.iter().filter(|t| t.name == "artifact_create").count(),
            1
        );
    }
}
