//! Persistent intent only. Runtime PID/creation tokens never enter the document.
use crate::{ApiError, graph::NodeId};
use serde::{Deserialize, Serialize};
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "mode", rename_all = "snake_case", deny_unknown_fields)]
pub enum CaptureSelection {
    Application { executable: String },
    System {},
}
impl CaptureSelection {
    pub fn validate(&self) -> Result<(), ApiError> {
        if let Self::Application { executable } = self {
            // Platform-neutral validation of Windows absolute executable paths.
            let b = executable.as_bytes();
            let absolute = (b.len() > 3
                && b[0].is_ascii_alphabetic()
                && b[1] == b':'
                && (b[2] == b'\\' || b[2] == b'/'))
                || executable.starts_with("\\\\");
            if !absolute
                || executable.len() >= 4096
                || executable.contains('\0')
                || !executable.to_ascii_lowercase().ends_with(".exe")
            {
                return Err(ApiError::invalid(
                    "Selecione um executável Windows com caminho absoluto.",
                ));
            }
        }
        Ok(())
    }
}
#[derive(Clone, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct CaptureResource {
    pub node_id: NodeId,
    pub selection: CaptureSelection,
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn selection_is_intent_not_pid_or_relative_path() {
        for path in ["C:\\Apps\\voice.exe", "\\\\server\\apps\\player.EXE"] {
            assert!(
                CaptureSelection::Application {
                    executable: path.into()
                }
                .validate()
                .is_ok()
            );
        }
        for path in [
            "voice.exe",
            "C:voice.exe",
            "/usr/bin/voice",
            "C:\\voice.dll",
            "C:\\bad\0.exe",
        ] {
            assert!(
                CaptureSelection::Application {
                    executable: path.into()
                }
                .validate()
                .is_err()
            );
        }
        assert!(
            serde_json::from_str::<CaptureSelection>(
                r#"{"mode":"application","executable":"C:/app.exe","process_id":123}"#
            )
            .is_err()
        );
        assert!(
            serde_json::from_str::<CaptureSelection>(r#"{"mode":"system","token":"123"}"#).is_err()
        );
        let intent = CaptureSelection::Application {
            executable: "C:/Apps/voice.exe".into(),
        };
        let encoded = serde_json::to_string(&intent).unwrap();
        assert_eq!(
            serde_json::from_str::<CaptureSelection>(&encoded).unwrap(),
            intent
        );
    }
}
