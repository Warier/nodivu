use crate::ApiError;
use serde::{Deserialize, Serialize};

/// Identidade opaca retornada pelo backend; nunca interpretada como índice/nome.
#[derive(Clone, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct DeviceId(pub String);

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Flow {
    Capture,
    Render,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum DeviceState {
    Active,
    Disabled,
    Unplugged,
    NotPresent,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum HardwareKind {
    Physical,
    Virtual,
    Unknown,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Support {
    Available,
    Unsupported,
    Unknown,
}

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct DeviceInfo {
    pub endpoint_id: DeviceId,
    pub name: String,
    pub flow: Flow,
    pub state: DeviceState,
    pub hardware_kind: HardwareKind,
    pub is_default: bool,
    pub support: Support,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub reason: Option<String>,
}

/// dB finitos dentro do intervalo da POC. Campo privado impede valores inválidos.
#[derive(Clone, Copy, Debug, PartialEq, Serialize, Deserialize)]
#[serde(try_from = "f32", into = "f32")]
pub struct GainDb(f32);

impl TryFrom<f32> for GainDb {
    type Error = ApiError;
    fn try_from(value: f32) -> Result<Self, Self::Error> {
        if value.is_finite() && (-60.0..=24.0).contains(&value) {
            Ok(Self(value))
        } else {
            Err(ApiError::invalid(
                "Ganho deve ser finito, entre −60 e +24 dB.",
            ))
        }
    }
}
impl From<GainDb> for f32 {
    fn from(value: GainDb) -> Self {
        value.0
    }
}
impl Default for GainDb {
    fn default() -> Self {
        Self(-12.0)
    }
}
