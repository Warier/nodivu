//! Contratos locais e validação pura; nenhum acesso a dispositivos ou à UI.
pub mod capture;
pub mod command;
pub mod graph;
pub mod json;
pub mod live_routing;
pub mod model;
pub mod project;
pub mod routing;

pub use command::*;
pub use model::*;

use serde::Serialize;

pub const PROTOCOL_VERSION: u32 = 1;
pub const MAX_LINE_BYTES: usize = 256 * 1024;
pub const MAX_PROJECT_BYTES: usize = 64 * 1024;
pub const MAX_JSON_DEPTH: usize = 16;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
#[serde(rename_all = "snake_case")]
pub enum ErrorCode {
    InvalidRequest,
    InvalidState,
    UnsupportedFormat,
    PermissionDenied,
    DeviceBusy,
    ProtocolVersionUnsupported,
    RevisionConflict,
    NotFound,
    DeviceUnavailable,
    UnsupportedCapability,
    ResourceLimit,
    BackendError,
    InternalError,
}

#[derive(Clone, Debug, Serialize)]
pub struct ApiError {
    pub code: ErrorCode,
    pub message: String,
    pub retryable: bool,
}

impl ApiError {
    pub fn new(code: ErrorCode, message: impl Into<String>) -> Self {
        Self {
            code,
            message: message.into(),
            retryable: false,
        }
    }
    pub fn invalid(message: impl Into<String>) -> Self {
        Self::new(ErrorCode::InvalidRequest, message)
    }
}

impl std::fmt::Display for ApiError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.message)
    }
}
impl std::error::Error for ApiError {}
