use crate::*;
use serde::{Deserialize, Serialize};
use serde_json::Value;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Request {
    pub protocol_version: u32,
    pub id: String,
    pub command: String,
    pub params: Value,
}

impl Request {
    pub fn validate(&self) -> Result<(), ApiError> {
        if self.protocol_version != PROTOCOL_VERSION {
            return Err(ApiError::new(
                ErrorCode::ProtocolVersionUnsupported,
                "Use protocol_version 1.",
            ));
        }
        if !(1..=64).contains(&self.id.chars().count()) || !self.params.is_object() {
            return Err(ApiError::invalid(
                "ID deve ter 1–64 caracteres; params deve ser objeto.",
            ));
        }
        Ok(())
    }
}

#[derive(Debug, Serialize)]
pub struct Response {
    protocol_version: u32,
    id: Option<String>,
    #[serde(flatten)]
    outcome: Outcome,
}
#[derive(Debug, Serialize)]
#[serde(untagged)]
enum Outcome {
    Success { ok: bool, result: Value },
    Failure { ok: bool, error: ApiError },
}

impl Response {
    pub fn new(id: Option<String>, result: Result<Value, ApiError>) -> Self {
        Self {
            protocol_version: PROTOCOL_VERSION,
            id,
            outcome: match result {
                Ok(result) => Outcome::Success { ok: true, result },
                Err(error) => Outcome::Failure { ok: false, error },
            },
        }
    }
    pub fn id(&self) -> Option<String> {
        self.id.clone()
    }
}
