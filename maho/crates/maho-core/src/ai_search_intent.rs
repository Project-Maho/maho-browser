#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum QueryIntent {
    AiPreferred,
    SerpPreferred,
    Ambiguous,
}

pub fn classify_query(query: &str) -> QueryIntent {
    let trimmed = query.trim();
    if trimmed.is_empty() {
        return QueryIntent::SerpPreferred;
    }

    let lower = trimmed.to_lowercase();

    // 1. Question mark is a clear indicator of AI preference
    if lower.contains('?') {
        return QueryIntent::AiPreferred;
    }

    // 2. Comparison/explanation request keywords
    let comparison_keywords = [
        "vs",
        "compare",
        "difference",
        "explain",
        "describe",
        "차이",
        "비교",
        "설명",
        "어떻게",
        "왜",
        "설명해줘",
    ];
    for &kw in &comparison_keywords {
        if lower.contains(kw) {
            return QueryIntent::AiPreferred;
        }
    }

    // Split query into words
    let words: Vec<&str> = lower.split_whitespace().collect();

    // 3. WH-words
    let wh_words = [
        "who", "what", "why", "how", "which", "where", "when", "whom", "whose",
    ];
    for word in &words {
        let clean_word = word.trim_matches(|c: char| !c.is_alphabetic());
        if wh_words.contains(&clean_word) {
            return QueryIntent::AiPreferred;
        }
    }

    // 4. Long sentence heuristic (>= 4 words)
    if words.len() >= 4 {
        return QueryIntent::AiPreferred;
    }

    // 5. SERP Fallbacks: local intent
    let local_keywords = ["near me", "근처", "맛집"];
    for &kw in &local_keywords {
        if lower.contains(kw) {
            return QueryIntent::SerpPreferred;
        }
    }

    // 6. SERP Fallbacks: famous sites and domains
    let famous_sites = [
        "google",
        "facebook",
        "twitter",
        "youtube",
        "github",
        "reddit",
        "wikipedia",
        "naver",
        "daum",
        "apple",
        "amazon",
        "netflix",
    ];
    if famous_sites.contains(&lower.as_str()) {
        return QueryIntent::SerpPreferred;
    }

    if lower.contains('.') && !lower.contains(' ') {
        return QueryIntent::SerpPreferred;
    }

    // 7. Short query fallback (<= 2 words)
    if words.len() <= 2 {
        return QueryIntent::SerpPreferred;
    }

    // 8. Default to Ambiguous
    QueryIntent::Ambiguous
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_classify_query() {
        // AI Preferred cases
        assert_eq!(
            classify_query("who is the president of US"),
            QueryIntent::AiPreferred
        );
        assert_eq!(classify_query("what is rust?"), QueryIntent::AiPreferred);
        assert_eq!(
            classify_query("how to build an iOS app"),
            QueryIntent::AiPreferred
        );
        assert_eq!(classify_query("rust vs go"), QueryIntent::AiPreferred);
        assert_eq!(
            classify_query("explain quantum computing"),
            QueryIntent::AiPreferred
        );
        assert_eq!(classify_query("차이점 설명해줘"), QueryIntent::AiPreferred);

        // SERP Preferred cases
        assert_eq!(classify_query("reddit"), QueryIntent::SerpPreferred);
        assert_eq!(
            classify_query("restaurants near me"),
            QueryIntent::SerpPreferred
        );
        assert_eq!(classify_query("weather today"), QueryIntent::SerpPreferred);
        assert_eq!(classify_query("daum"), QueryIntent::SerpPreferred);
        assert_eq!(classify_query("google.com"), QueryIntent::SerpPreferred);

        // Ambiguous cases
        assert_eq!(
            classify_query("rust programming language"),
            QueryIntent::Ambiguous
        );
        assert_eq!(
            classify_query("best mechanical keyboard"),
            QueryIntent::Ambiguous
        );
    }
}
