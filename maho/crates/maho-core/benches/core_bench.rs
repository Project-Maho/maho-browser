use criterion::{criterion_group, criterion_main, Criterion};
use maho_core::maho_core::MahoCore;
use maho_types::common::Url;
use maho_types::events::shell_event::ShellEvent;
use maho_types::identifiers::SpaceId;
use maho_types::space::SpaceColor;

fn bench_create_tab(c: &mut Criterion) {
    c.bench_function("create_tab", |b| {
        b.iter_with_setup(MahoCore::new, |mut core| {
            core.handle_event(ShellEvent::CreateTab {
                space_id: SpaceId("space-1".into()),
                url: Some(Url("https://example.com".into())),
                parent_id: None,
                tab_id: None,
                window_id: None,
                is_private: false,
            });
        });
    });
}

fn bench_close_tab(c: &mut Criterion) {
    c.bench_function("close_tab", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                core.handle_event(ShellEvent::CreateTab {
                    space_id: SpaceId("space-1".into()),
                    url: Some(Url("https://example.com".into())),
                    parent_id: None,
                    tab_id: None,
                    window_id: None,
                    is_private: false,
                });
                let tab_id = core.get_tab_view_models().first().map(|t| t.id.clone());
                (core, tab_id)
            },
            |(mut core, tab_id)| {
                if let Some(id) = tab_id {
                    core.handle_event(ShellEvent::CloseTab {
                        tab_id: id,
                        expected_space_id: None,
                    });
                }
            },
        );
    });
}

fn bench_switch_space(c: &mut Criterion) {
    c.bench_function("switch_space", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                let profile_id = core
                    .get_active_profile_id()
                    .cloned()
                    .unwrap_or_else(|| maho_types::identifiers::ProfileId::new(""));
                core.handle_event(ShellEvent::CreateSpace {
                    name: "Test".into(),
                    color: SpaceColor {
                        hue: 220.0,
                        saturation: 0.8,
                        brightness: 0.9,
                        grain: 0.0,
                    },
                    profile_id,
                });
                let spaces = core.get_space_view_models();
                let target = spaces
                    .last()
                    .map(|s| s.id.clone())
                    .unwrap_or(SpaceId("s".into()));
                (core, target)
            },
            |(mut core, target)| {
                core.handle_event(ShellEvent::ActivateSpace { space_id: target });
            },
        );
    });
}

fn bench_command_bar_query(c: &mut Criterion) {
    c.bench_function("command_bar_query", |b| {
        b.iter_with_setup(
            || {
                let mut core = MahoCore::new();
                for i in 0..100 {
                    core.handle_event(ShellEvent::CreateTab {
                        space_id: SpaceId("space-1".into()),
                        url: Some(Url(format!("https://example.com/{i}"))),
                        parent_id: None,
                        tab_id: None,
                        window_id: None,
                        is_private: false,
                    });
                }
                core
            },
            |mut core| {
                core.handle_event(ShellEvent::CommandBarQuery {
                    text: "example".into(),
                    mode: None,
                    is_incognito: false,
                });
            },
        );
    });
}

criterion_group!(
    benches,
    bench_create_tab,
    bench_close_tab,
    bench_switch_space,
    bench_command_bar_query,
);
criterion_main!(benches);
