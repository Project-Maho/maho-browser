use std::time::Instant;

pub mod browser_cli;

#[derive(Debug, Clone)]
pub struct BenchResult {
    pub library: String,
    pub operation: String,
    pub duration_us: u64,
    pub encoded_size_bytes: usize,
}

pub mod yrs_bench {
    use super::*;
    use yrs::updates::decoder::Decode;
    use yrs::{Doc, Map, MapPrelim, ReadTxn, Transact};

    pub fn insert_tab_metadata(n: usize) -> BenchResult {
        let doc = Doc::new();
        let start = Instant::now();
        {
            let tabs = doc.get_or_insert_map("tabs");
            let mut txn = doc.transact_mut();
            for i in 0..n {
                let key: std::sync::Arc<str> = format!("tab-{i}").into();
                let tab = tabs.insert(&mut txn, key, MapPrelim::default());
                tab.insert(&mut txn, "url", format!("https://example.com/{i}"));
                tab.insert(&mut txn, "title", format!("Tab {i}"));
                tab.insert(&mut txn, "state", "active");
                tab.insert(&mut txn, "space_id", "space-1");
            }
        }
        let duration = start.elapsed();
        let txn = doc.transact();
        let encoded = txn.encode_state_as_update_v1(&yrs::StateVector::default());
        BenchResult {
            library: "yrs".into(),
            operation: "insert".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded.len(),
        }
    }

    pub fn merge_two_peers(n: usize) -> BenchResult {
        let doc1 = Doc::with_client_id(1);
        let doc2 = Doc::with_client_id(2);
        {
            let tabs1 = doc1.get_or_insert_map("tabs");
            let mut txn1 = doc1.transact_mut();
            for i in 0..n / 2 {
                let key: std::sync::Arc<str> = format!("tab-a-{i}").into();
                let tab = tabs1.insert(&mut txn1, key, MapPrelim::default());
                tab.insert(&mut txn1, "url", format!("https://a.com/{i}"));
                tab.insert(&mut txn1, "title", format!("A {i}"));
            }
        }
        {
            let tabs2 = doc2.get_or_insert_map("tabs");
            let mut txn2 = doc2.transact_mut();
            for i in 0..n / 2 {
                let key: std::sync::Arc<str> = format!("tab-b-{i}").into();
                let tab = tabs2.insert(&mut txn2, key, MapPrelim::default());
                tab.insert(&mut txn2, "url", format!("https://b.com/{i}"));
                tab.insert(&mut txn2, "title", format!("B {i}"));
            }
        }
        let sv1 = doc1.transact().state_vector();
        let sv2 = doc2.transact().state_vector();
        let update1 = doc1.transact().encode_state_as_update_v1(&sv2);
        let update2 = doc2.transact().encode_state_as_update_v1(&sv1);
        let start = Instant::now();
        {
            let mut txn1 = doc1.transact_mut();
            txn1.apply_update(yrs::Update::decode_v1(&update2).unwrap())
                .ok();
        }
        {
            let mut txn2 = doc2.transact_mut();
            txn2.apply_update(yrs::Update::decode_v1(&update1).unwrap())
                .ok();
        }
        let duration = start.elapsed();
        BenchResult {
            library: "yrs".into(),
            operation: "merge".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: update1.len() + update2.len(),
        }
    }

    pub fn cold_load(n: usize) -> BenchResult {
        let doc = Doc::new();
        {
            let tabs = doc.get_or_insert_map("tabs");
            let mut txn = doc.transact_mut();
            for i in 0..n {
                let key: std::sync::Arc<str> = format!("tab-{i}").into();
                let tab = tabs.insert(&mut txn, key, MapPrelim::default());
                tab.insert(&mut txn, "url", format!("https://example.com/{i}"));
                tab.insert(&mut txn, "title", format!("Tab {i}"));
                tab.insert(&mut txn, "state", "active");
            }
        }
        let encoded = doc
            .transact()
            .encode_state_as_update_v1(&yrs::StateVector::default());
        let start = Instant::now();
        let doc2 = Doc::new();
        {
            let mut txn = doc2.transact_mut();
            txn.apply_update(yrs::Update::decode_v1(&encoded).unwrap())
                .ok();
        }
        let duration = start.elapsed();
        BenchResult {
            library: "yrs".into(),
            operation: "cold_load".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded.len(),
        }
    }
}

pub mod automerge_bench {
    use super::*;
    use automerge::transaction::Transactable;
    use automerge::{AutoCommit, ObjType, ROOT};

    pub fn insert_tab_metadata(n: usize) -> BenchResult {
        let mut doc = AutoCommit::new();
        let start = Instant::now();
        let tabs = doc.put_object(ROOT, "tabs", ObjType::Map).unwrap();
        for i in 0..n {
            let tab = doc
                .put_object(&tabs, format!("tab-{i}"), ObjType::Map)
                .unwrap();
            doc.put(&tab, "url", format!("https://example.com/{i}"))
                .unwrap();
            doc.put(&tab, "title", format!("Tab {i}")).unwrap();
            doc.put(&tab, "state", "active").unwrap();
            doc.put(&tab, "space_id", "space-1").unwrap();
        }
        let duration = start.elapsed();
        let encoded = doc.save();
        BenchResult {
            library: "automerge".into(),
            operation: "insert".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded.len(),
        }
    }

    pub fn merge_two_peers(n: usize) -> BenchResult {
        let mut doc1 = AutoCommit::new();
        let mut doc2 = doc1.fork();
        let tabs1 = doc1.put_object(ROOT, "tabs", ObjType::Map).unwrap();
        for i in 0..n / 2 {
            let tab = doc1
                .put_object(&tabs1, format!("tab-a-{i}"), ObjType::Map)
                .unwrap();
            doc1.put(&tab, "url", format!("https://a.com/{i}")).unwrap();
            doc1.put(&tab, "title", format!("A {i}")).unwrap();
        }
        let tabs2 = doc2.put_object(ROOT, "tabs", ObjType::Map).unwrap();
        for i in 0..n / 2 {
            let tab = doc2
                .put_object(&tabs2, format!("tab-b-{i}"), ObjType::Map)
                .unwrap();
            doc2.put(&tab, "url", format!("https://b.com/{i}")).unwrap();
            doc2.put(&tab, "title", format!("B {i}")).unwrap();
        }
        let start = Instant::now();
        doc1.merge(&mut doc2).unwrap();
        let duration = start.elapsed();
        let encoded = doc1.save();
        BenchResult {
            library: "automerge".into(),
            operation: "merge".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded.len(),
        }
    }

    pub fn cold_load(n: usize) -> BenchResult {
        let mut doc = AutoCommit::new();
        let tabs = doc.put_object(ROOT, "tabs", ObjType::Map).unwrap();
        for i in 0..n {
            let tab = doc
                .put_object(&tabs, format!("tab-{i}"), ObjType::Map)
                .unwrap();
            doc.put(&tab, "url", format!("https://example.com/{i}"))
                .unwrap();
            doc.put(&tab, "title", format!("Tab {i}")).unwrap();
            doc.put(&tab, "state", "active").unwrap();
        }
        let encoded = doc.save();
        let start = Instant::now();
        let _doc2 = AutoCommit::load(&encoded).unwrap();
        let duration = start.elapsed();
        BenchResult {
            library: "automerge".into(),
            operation: "cold_load".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded.len(),
        }
    }
}

pub mod diamond_bench {
    use super::*;
    use diamond_types_extended::{Document, Frontier, Uuid};

    fn encoded_size(ops: &diamond_types_extended::SerializedOpsOwned) -> usize {
        match postcard::to_allocvec(ops) {
            Ok(bytes) => bytes.len(),
            Err(_) => 0,
        }
    }

    pub fn insert_tab_metadata(n: usize) -> BenchResult {
        let mut doc = Document::new();
        let agent = doc.create_agent(Uuid::new_v4());
        let start = Instant::now();
        {
            let mut w = doc.writer(agent);
            w.root_create_map("tabs");
            for i in 0..n {
                let tab_key = format!("tab-{i}");
                w.create_map(&["tabs"], &tab_key);
                w.set(
                    &["tabs", &tab_key],
                    "url",
                    format!("https://example.com/{i}"),
                );
                w.set(&["tabs", &tab_key], "title", format!("Tab {i}"));
                w.set(&["tabs", &tab_key], "state", "active");
                w.set(&["tabs", &tab_key], "space_id", "space-1");
            }
        }
        let duration = start.elapsed();
        let ops = doc.ops_since_owned(&Frontier::root());
        let encoded_size = encoded_size(&ops);
        BenchResult {
            library: "diamond-types-extended".into(),
            operation: "insert".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: encoded_size,
        }
    }

    pub fn merge_two_peers(n: usize) -> BenchResult {
        let mut doc1 = Document::new();
        let agent1 = doc1.create_agent(Uuid::new_v4());
        {
            let mut w = doc1.writer(agent1);
            w.root_create_map("tabs");
            for i in 0..n / 2 {
                let tab_key = format!("tab-a-{i}");
                w.create_map(&["tabs"], &tab_key);
                w.set(&["tabs", &tab_key], "url", format!("https://a.com/{i}"));
                w.set(&["tabs", &tab_key], "title", format!("A {i}"));
            }
        }

        let mut doc2 = Document::new();
        let agent2 = doc2.create_agent(Uuid::new_v4());
        {
            let mut w = doc2.writer(agent2);
            w.root_create_map("tabs");
            for i in 0..n / 2 {
                let tab_key = format!("tab-b-{i}");
                w.create_map(&["tabs"], &tab_key);
                w.set(&["tabs", &tab_key], "url", format!("https://b.com/{i}"));
                w.set(&["tabs", &tab_key], "title", format!("B {i}"));
            }
        }

        let ops1 = doc1.ops_since_owned(&Frontier::root());
        let ops2 = doc2.ops_since_owned(&Frontier::root());
        let ops1_bytes = postcard::to_allocvec(&ops1).unwrap_or_default();
        let ops2_bytes = postcard::to_allocvec(&ops2).unwrap_or_default();

        let start = Instant::now();
        let _ = doc1.merge_ops(ops2);
        let duration = start.elapsed();

        BenchResult {
            library: "diamond-types-extended".into(),
            operation: "merge".into(),
            duration_us: duration.as_micros() as u64,
            encoded_size_bytes: ops1_bytes.len() + ops2_bytes.len(),
        }
    }

    pub fn cold_load(n: usize) -> BenchResult {
        let (result, restored) = cold_load_document(n);
        // Validate usable state outside the measured reconstruction interval.
        if n > 0 {
            assert_eq!(
                restored.get_str(&["tabs", "tab-0"], "title"),
                Some("Tab 0".into())
            );
        }
        result
    }

    fn cold_load_document(n: usize) -> (BenchResult, Document) {
        let mut doc = Document::new();
        let agent = doc.create_agent(Uuid::new_v4());
        {
            let mut w = doc.writer(agent);
            w.root_create_map("tabs");
            for i in 0..n {
                let tab_key = format!("tab-{i}");
                w.create_map(&["tabs"], &tab_key);
                w.set(
                    &["tabs", &tab_key],
                    "url",
                    format!("https://example.com/{i}"),
                );
                w.set(&["tabs", &tab_key], "title", format!("Tab {i}"));
                w.set(&["tabs", &tab_key], "state", "active");
                w.set(&["tabs", &tab_key], "space_id", "space-1");
            }
        }

        let ops = doc.ops_since_owned(&Frontier::root());
        let encoded = postcard::to_allocvec(&ops).unwrap_or_default();

        let start = Instant::now();
        let restored_ops: diamond_types_extended::SerializedOpsOwned =
            postcard::from_bytes(&encoded).unwrap();
        let mut restored = Document::new();
        restored
            .merge_ops(restored_ops)
            .expect("restore encoded operations");
        let duration = start.elapsed();

        (
            BenchResult {
                library: "diamond-types-extended".into(),
                operation: "cold_load".into(),
                duration_us: duration.as_micros() as u64,
                encoded_size_bytes: encoded.len(),
            },
            restored,
        )
    }

    #[cfg(test)]
    mod tests {
        #[test]
        fn cold_load_reconstructs_usable_document() {
            let (_, restored) = super::cold_load_document(3);
            assert_eq!(
                restored.get_str(&["tabs", "tab-2"], "url"),
                Some("https://example.com/2".into())
            );
            assert_eq!(
                restored.get_str(&["tabs", "tab-0"], "title"),
                Some("Tab 0".into())
            );
        }
    }
}

pub fn run_comparison(n: usize) -> Vec<BenchResult> {
    vec![
        yrs_bench::insert_tab_metadata(n),
        automerge_bench::insert_tab_metadata(n),
        diamond_bench::insert_tab_metadata(n),
        yrs_bench::merge_two_peers(n),
        automerge_bench::merge_two_peers(n),
        diamond_bench::merge_two_peers(n),
        yrs_bench::cold_load(n),
        automerge_bench::cold_load(n),
        diamond_bench::cold_load(n),
    ]
}

pub fn print_comparison(results: &[BenchResult]) {
    println!(
        "{:<14} {:<12} {:>12} {:>12}",
        "Library", "Operation", "Time (μs)", "Size (bytes)"
    );
    println!("{}", "-".repeat(54));
    for r in results {
        println!(
            "{:<14} {:<12} {:>12} {:>12}",
            r.library, r.operation, r.duration_us, r.encoded_size_bytes
        );
    }
}
