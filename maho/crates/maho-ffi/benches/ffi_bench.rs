use std::ffi::{CStr, CString};

use criterion::{criterion_group, criterion_main, Criterion};
use maho_core::maho_core::MahoCore;
use maho_ffi::{
    maho_core_free, maho_core_get_space_view_models, maho_core_get_tab_view_models,
    maho_core_handle_event, maho_core_new, maho_string_free,
};

fn ffi_roundtrip(core: *mut MahoCore, event_json: &str) {
    let c_event = CString::new(event_json).unwrap();
    unsafe {
        let result = maho_core_handle_event(core, c_event.as_ptr());
        if !result.is_null() {
            let _ = CStr::from_ptr(result).to_str();
            maho_string_free(result);
        }
    }
}

fn bench_ffi_create_tab_roundtrip(c: &mut Criterion) {
    c.bench_function("ffi_create_tab_roundtrip", |b| {
        b.iter_with_setup(
            || maho_core_new(),
            |core| {
                ffi_roundtrip(
                    core,
                    r#"{"kind":"create_tab","space_id":"space-1","url":"https://example.com"}"#,
                );
                unsafe { maho_core_free(core) };
            },
        );
    });
}

fn bench_ffi_get_tab_view_models(c: &mut Criterion) {
    c.bench_function("ffi_get_tab_view_models", |b| {
        b.iter_with_setup(
            || {
                let core = maho_core_new();
                for i in 0..50 {
                    let json = format!(
                        r#"{{"kind":"create_tab","space_id":"space-1","url":"https://example.com/{i}"}}"#
                    );
                    ffi_roundtrip(core, &json);
                }
                core
            },
            |core| {
                unsafe {
                    let result = maho_core_get_tab_view_models(core);
                    if !result.is_null() {
                        let _ = CStr::from_ptr(result).to_str();
                        maho_string_free(result);
                    }
                    maho_core_free(core);
                }
            },
        );
    });
}

fn bench_ffi_get_space_view_models(c: &mut Criterion) {
    c.bench_function("ffi_get_space_view_models", |b| {
        b.iter_with_setup(
            || maho_core_new(),
            |core| unsafe {
                let result = maho_core_get_space_view_models(core);
                if !result.is_null() {
                    let _ = CStr::from_ptr(result).to_str();
                    maho_string_free(result);
                }
                maho_core_free(core);
            },
        );
    });
}

fn bench_ffi_json_serialization(c: &mut Criterion) {
    c.bench_function("ffi_handle_event_json_deser", |b| {
        b.iter_with_setup(
            || maho_core_new(),
            |core| {
                ffi_roundtrip(
                    core,
                    r#"{"kind":"command_bar_query","text":"example search query"}"#,
                );
                unsafe { maho_core_free(core) };
            },
        );
    });
}

criterion_group!(
    benches,
    bench_ffi_create_tab_roundtrip,
    bench_ffi_get_tab_view_models,
    bench_ffi_get_space_view_models,
    bench_ffi_json_serialization,
);
criterion_main!(benches);
