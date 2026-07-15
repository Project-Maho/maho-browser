use serde::Deserialize;

#[derive(Deserialize, Debug, PartialEq)]
pub struct RawFact {
    pub fact: String,
    pub category: String,
    pub importance: f32,
}

#[derive(Deserialize, Debug)]
struct FactsEnvelope {
    #[serde(default)]
    facts: Vec<RawFact>,
}

#[test]
fn test_json_parse_reliability() {
    let raw_response = r#"{
        "facts": [
            {"fact": "User prefers writing code in Rust", "category": "preference", "importance": 0.9},
            {"fact": "User lives in Tokyo", "category": "knowledge", "importance": 0.8}
        ]
    }"#;

    let env: FactsEnvelope = serde_json::from_str(raw_response).unwrap();
    assert_eq!(env.facts.len(), 2);
    assert_eq!(env.facts[0].fact, "User prefers writing code in Rust");
    assert_eq!(env.facts[0].category, "preference");
    assert_eq!(env.facts[0].importance, 0.9);
}
