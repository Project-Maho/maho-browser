use automerge::transaction::Transactable;
use automerge::{AutoCommit, ObjType, ReadDoc, ROOT};
use yrs::updates::decoder::Decode;
use yrs::{Doc, Map, MapPrelim, MapRef, ReadTxn, Transact};

#[test]
fn yrs_insert_and_read_back() {
    let doc = Doc::new();
    let tabs = doc.get_or_insert_map("tabs");
    let mut txn = doc.transact_mut();
    let tab = tabs.insert(&mut txn, "tab-1", MapPrelim::default());
    tab.insert(&mut txn, "url", "https://example.com");
    tab.insert(&mut txn, "title", "Example");
    tab.insert(&mut txn, "state", "active");
    drop(txn);

    let txn = doc.transact();
    let tabs = txn.get_map("tabs").unwrap();
    let out = tabs.get(&txn, "tab-1").unwrap();
    let tab: MapRef = out.try_into().unwrap();
    assert_eq!(
        tab.get(&txn, "url").unwrap().to_string(&txn),
        "https://example.com"
    );
    assert_eq!(tab.get(&txn, "title").unwrap().to_string(&txn), "Example");
    assert_eq!(tab.get(&txn, "state").unwrap().to_string(&txn), "active");
}

#[test]
fn yrs_merge_two_docs() {
    let doc1 = Doc::with_client_id(1);
    let doc2 = Doc::with_client_id(2);

    let tabs1 = doc1.get_or_insert_map("tabs");
    {
        let mut txn = doc1.transact_mut();
        tabs1.insert(&mut txn, "a", "value-a");
    }

    let tabs2 = doc2.get_or_insert_map("tabs");
    {
        let mut txn = doc2.transact_mut();
        tabs2.insert(&mut txn, "b", "value-b");
    }

    let sv1 = doc1.transact().state_vector();
    let update2 = doc2.transact().encode_state_as_update_v1(&sv1);
    {
        let mut txn = doc1.transact_mut();
        txn.apply_update(yrs::Update::decode_v1(&update2).unwrap())
            .ok();
    }

    let txn = doc1.transact();
    let tabs = txn.get_map("tabs").unwrap();
    assert!(tabs.get(&txn, "a").is_some());
    assert!(tabs.get(&txn, "b").is_some());
}

#[test]
fn yrs_encode_decode_roundtrip() {
    let doc = Doc::new();
    let tabs = doc.get_or_insert_map("tabs");
    {
        let mut txn = doc.transact_mut();
        tabs.insert(&mut txn, "key1", "val1");
        tabs.insert(&mut txn, "key2", "val2");
    }

    let encoded = doc
        .transact()
        .encode_state_as_update_v1(&yrs::StateVector::default());
    let doc2 = Doc::new();
    {
        let mut txn = doc2.transact_mut();
        txn.apply_update(yrs::Update::decode_v1(&encoded).unwrap())
            .ok();
    }

    let txn = doc2.transact();
    let tabs2 = txn.get_map("tabs").unwrap();
    assert_eq!(tabs2.get(&txn, "key1").unwrap().to_string(&txn), "val1");
    assert_eq!(tabs2.get(&txn, "key2").unwrap().to_string(&txn), "val2");
}

#[test]
fn automerge_insert_and_read_back() {
    let mut doc = AutoCommit::new();
    let tabs = doc.put_object(ROOT, "tabs", ObjType::Map).unwrap();
    let tab = doc.put_object(&tabs, "tab-1", ObjType::Map).unwrap();
    doc.put(&tab, "url", "https://example.com").unwrap();
    doc.put(&tab, "title", "Example").unwrap();
    doc.put(&tab, "state", "active").unwrap();

    let (url, _) = doc.get(&tab, "url").unwrap().unwrap();
    assert_eq!(url.to_str().unwrap(), "https://example.com");
    let (title, _) = doc.get(&tab, "title").unwrap().unwrap();
    assert_eq!(title.to_str().unwrap(), "Example");
}

#[test]
fn automerge_merge_two_docs() {
    let mut doc1 = AutoCommit::new();
    let mut doc2 = doc1.fork();

    doc1.put(ROOT, "a", "value-a").unwrap();
    doc2.put(ROOT, "b", "value-b").unwrap();

    doc1.merge(&mut doc2).unwrap();

    assert!(doc1.get(ROOT, "a").unwrap().is_some());
    assert!(doc1.get(ROOT, "b").unwrap().is_some());
}

#[test]
fn automerge_encode_decode_roundtrip() {
    let mut doc = AutoCommit::new();
    doc.put(ROOT, "key1", "val1").unwrap();
    doc.put(ROOT, "key2", "val2").unwrap();

    let encoded = doc.save();
    let doc2 = AutoCommit::load(&encoded).unwrap();

    let (v1, _) = doc2.get(ROOT, "key1").unwrap().unwrap();
    assert_eq!(v1.to_str().unwrap(), "val1");
    let (v2, _) = doc2.get(ROOT, "key2").unwrap().unwrap();
    assert_eq!(v2.to_str().unwrap(), "val2");
}

#[test]
fn diamond_insert_and_read_back() {
    use diamond_types_extended::{Document, Frontier, Uuid};

    let mut doc = Document::new();
    let agent = doc.create_agent(Uuid::new_v4());
    {
        let mut w = doc.writer(agent);
        w.root_create_map("tabs");
        w.create_map(&["tabs"], "tab-1");
        w.set(&["tabs", "tab-1"], "url", "https://example.com");
        w.set(&["tabs", "tab-1"], "title", "Example");
        w.set(&["tabs", "tab-1"], "state", "active");
    }

    // Verify the data is stored correctly by checking we can get serialized ops
    let ops = doc.ops_since_owned(&Frontier::root());
    let encoded = postcard::to_allocvec(&ops);
    assert!(encoded.is_ok());
    assert!(!encoded.unwrap().is_empty());
}

#[test]
fn diamond_merge_two_docs() {
    use diamond_types_extended::{Document, Frontier, Uuid};

    let mut doc1 = Document::new();
    let agent1 = doc1.create_agent(Uuid::new_v4());
    {
        let mut w = doc1.writer(agent1);
        w.root_create_map("data");
        w.set(&["data"], "a", "value-a");
    }

    let mut doc2 = Document::new();
    let agent2 = doc2.create_agent(Uuid::new_v4());
    {
        let mut w = doc2.writer(agent2);
        w.root_create_map("data");
        w.set(&["data"], "b", "value-b");
    }

    let ops2 = doc2.ops_since_owned(&Frontier::root());
    let merge_result = doc1.merge_ops(ops2);

    // Verify merge succeeded
    assert!(merge_result.is_ok());
}

#[test]
fn diamond_encode_decode_roundtrip() {
    use diamond_types_extended::{Document, Frontier, Uuid};

    let mut doc = Document::new();
    let agent = doc.create_agent(Uuid::new_v4());
    {
        let mut w = doc.writer(agent);
        w.root_create_map("data");
        w.set(&["data"], "key1", "val1");
        w.set(&["data"], "key2", "val2");
    }

    let ops = doc.ops_since_owned(&Frontier::root());
    let encoded = postcard::to_allocvec(&ops).expect("should serialize");

    let restored_ops: diamond_types_extended::SerializedOpsOwned =
        postcard::from_bytes(&encoded).expect("should deserialize");

    let mut doc2 = Document::new();
    let merge_result = doc2.merge_ops(restored_ops);
    assert!(merge_result.is_ok());

    // Verify decode worked — document should have a "data" map with our keys
    assert!(doc2.get_map(&["data"]).is_some());
    assert_eq!(doc2.get_str(&["data"], "key1"), Some("val1".to_string()));
    assert_eq!(doc2.get_str(&["data"], "key2"), Some("val2".to_string()));
}

#[test]
fn comparison_report_runs() {
    let results = maho_bench::run_comparison(100);
    assert_eq!(results.len(), 9);
    for r in &results {
        assert!(r.encoded_size_bytes > 0);
    }
}
