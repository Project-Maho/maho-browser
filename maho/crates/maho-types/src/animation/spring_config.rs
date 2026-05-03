use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpringConfig {
    /// 0-1 (1 = critically damped)
    pub damping_ratio: f64,
    /// N/m
    pub stiffness: f64,
    /// kg
    pub mass: f64,
    /// m/s
    pub initial_velocity: f64,
}

pub const SPRING_SNAPPY: SpringConfig = SpringConfig {
    damping_ratio: 0.85,
    stiffness: 300.0,
    mass: 1.0,
    initial_velocity: 0.0,
};

pub const SPRING_SMOOTH: SpringConfig = SpringConfig {
    damping_ratio: 0.9,
    stiffness: 200.0,
    mass: 1.0,
    initial_velocity: 0.0,
};

pub const SPRING_BOUNCY: SpringConfig = SpringConfig {
    damping_ratio: 0.6,
    stiffness: 250.0,
    mass: 1.0,
    initial_velocity: 0.0,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(rename_all = "lowercase")]
pub enum SpringPresetName {
    Snappy,
    Smooth,
    Bouncy,
}

impl SpringPresetName {
    pub fn config(&self) -> &SpringConfig {
        match self {
            Self::Snappy => &SPRING_SNAPPY,
            Self::Smooth => &SPRING_SMOOTH,
            Self::Bouncy => &SPRING_BOUNCY,
        }
    }
}
