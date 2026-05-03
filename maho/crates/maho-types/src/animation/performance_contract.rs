use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PerformanceOwner {
    Engine,
    Surface,
    Both,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum PercentileTarget {
    P95,
    P99,
    Sustained,
}

#[derive(Clone, Debug)]
pub struct PerformanceMetric {
    pub name: &'static str,
    pub target: u64,
    pub unit: &'static str,
    pub percentile: &'static str,
    pub owner: &'static str,
}

pub const PERFORMANCE_CONTRACT: [PerformanceMetric; 11] = [
    PerformanceMetric {
        name: "tab_switch_active_to_active",
        target: 16,
        unit: "ms",
        percentile: "p99",
        owner: "engine",
    },
    PerformanceMetric {
        name: "tab_switch_suspended_to_active",
        target: 300,
        unit: "ms",
        percentile: "p99",
        owner: "engine",
    },
    PerformanceMetric {
        name: "command_bar_appear",
        target: 5,
        unit: "ms",
        percentile: "p99",
        owner: "surface",
    },
    PerformanceMetric {
        name: "command_bar_search",
        target: 50,
        unit: "ms",
        percentile: "p99",
        owner: "engine",
    },
    PerformanceMetric {
        name: "sidebar_render_500_tabs",
        target: 60,
        unit: "fps",
        percentile: "sustained",
        owner: "surface",
    },
    PerformanceMetric {
        name: "memory_per_tab_webkit",
        target: 52_428_800,
        unit: "bytes",
        percentile: "p95",
        owner: "engine",
    },
    PerformanceMetric {
        name: "memory_per_tab_chromium",
        target: 83_886_080,
        unit: "bytes",
        percentile: "p95",
        owner: "engine",
    },
    PerformanceMetric {
        name: "cold_launch_to_interactive",
        target: 800,
        unit: "ms",
        percentile: "p95",
        owner: "both",
    },
    PerformanceMetric {
        name: "space_switch",
        target: 100,
        unit: "ms",
        percentile: "p99",
        owner: "both",
    },
    PerformanceMetric {
        name: "history_search_10k",
        target: 50,
        unit: "ms",
        percentile: "p99",
        owner: "engine",
    },
    PerformanceMetric {
        name: "bookmark_search_5k",
        target: 20,
        unit: "ms",
        percentile: "p99",
        owner: "engine",
    },
];

pub struct TabRestoreTarget {
    pub target_ms: i64,
    pub percentile: &'static str,
}

pub struct TabRestoreTargets {
    pub frozen_to_active: TabRestoreTarget,
    pub suspended_to_active: TabRestoreTarget,
    pub archived_to_active: TabRestoreTarget,
}

pub const TAB_RESTORE_TARGETS: TabRestoreTargets = TabRestoreTargets {
    frozen_to_active: TabRestoreTarget {
        target_ms: 16,
        percentile: "p99",
    },
    suspended_to_active: TabRestoreTarget {
        target_ms: 300,
        percentile: "p99",
    },
    archived_to_active: TabRestoreTarget {
        target_ms: -1,
        percentile: "p99",
    },
};

pub struct TabLifecycleTimeouts {
    pub active_to_frozen_minutes: u64,
    pub frozen_to_suspended_minutes: u64,
    pub suspended_to_archived_hours: u64,
}

pub const TAB_LIFECYCLE_TIMEOUTS: TabLifecycleTimeouts = TabLifecycleTimeouts {
    active_to_frozen_minutes: 5,
    frozen_to_suspended_minutes: 15,
    suspended_to_archived_hours: 24,
};

pub struct MemoryTargets {
    pub webkit_per_tab_bytes: u64,
    pub chromium_per_tab_bytes: u64,
    pub frozen_per_tab_bytes: u64,
    pub suspended_per_tab_bytes: u64,
    pub archived_per_tab_bytes: u64,
}

pub const MEMORY_TARGETS: MemoryTargets = MemoryTargets {
    webkit_per_tab_bytes: 52_428_800,
    chromium_per_tab_bytes: 83_886_080,
    frozen_per_tab_bytes: 52_428_800,
    suspended_per_tab_bytes: 0,
    archived_per_tab_bytes: 500,
};
