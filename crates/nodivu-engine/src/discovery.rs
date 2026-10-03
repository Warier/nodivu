use nodivu_core::DeviceInfo;
use std::{error::Error, fmt};
use uuid::Uuid;

/// A partial inventory is explicit: unusable endpoints are never fabricated.
#[derive(Default, serde::Serialize)]
pub struct DeviceInventory {
    pub devices: Vec<DeviceInfo>,
    pub warnings: Vec<String>,
}
impl DeviceInventory {
    #[cfg(any(windows, test))]
    pub(crate) fn merge_flows(
        flows: impl IntoIterator<Item = Result<Self, BackendError>>,
    ) -> Result<Self, BackendError> {
        let mut result = Self::default();
        let mut available = false;
        for flow in flows {
            match flow {
                Ok(mut inventory) => {
                    available = true;
                    result.devices.append(&mut inventory.devices);
                    result.warnings.append(&mut inventory.warnings);
                }
                Err(error) => result.warnings.push(error.to_string()),
            }
        }
        if !available {
            return Err(BackendError::new(
                "Descoberta de áudio indisponível",
                std::io::Error::other(result.warnings.join("; ")),
            ));
        }
        Ok(result)
    }
}

#[cfg(test)]
mod inventory_tests {
    use super::*;

    fn failed() -> Result<DeviceInventory, BackendError> {
        Err(BackendError::new(
            "Enumerar entradas",
            std::io::Error::other("0xE000020B"),
        ))
    }
    #[test]
    fn failed_flow_keeps_other_flow_and_reports_warning() {
        let device = DeviceInfo {
            endpoint_id: nodivu_core::DeviceId("actual-output".into()),
            name: "Output".into(),
            flow: nodivu_core::Flow::Render,
            state: nodivu_core::DeviceState::Active,
            hardware_kind: nodivu_core::HardwareKind::Unknown,
            support: nodivu_core::Support::Unknown,
            is_default: false,
            reason: None,
        };
        let result = DeviceInventory::merge_flows([
            failed(),
            Ok(DeviceInventory {
                devices: vec![device],
                warnings: vec!["Default indisponível".into()],
            }),
        ])
        .expect("partial inventory");
        assert_eq!(result.devices[0].endpoint_id.0, "actual-output");
        assert_eq!(result.warnings.len(), 2);
        assert!(result.warnings[0].contains("0xE000020B"));
        assert!(!result.devices[0].is_default);
    }
    #[test]
    fn total_discovery_failure_is_not_success_with_empty_devices() {
        let result = DeviceInventory::merge_flows([failed(), failed()]);
        assert!(
            result
                .err()
                .expect("failed inventory")
                .to_string()
                .contains("0xE000020B")
        );
    }
}

#[derive(Debug)]
pub struct BackendError {
    pub operation: &'static str,
    cause: Box<dyn Error + Send + Sync>,
}
impl BackendError {
    pub fn new(operation: &'static str, cause: impl Error + Send + Sync + 'static) -> Self {
        Self {
            operation,
            cause: Box::new(cause),
        }
    }
}
impl fmt::Display for BackendError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}: {}", self.operation, self.cause)
    }
}
impl Error for BackendError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        Some(self.cause.as_ref())
    }
}

/// Substituído somente por um backend falso identificado nos testes offline.
pub trait DeviceBackend {
    fn device_inventory(&mut self) -> Result<DeviceInventory, BackendError> {
        self.enumerate().map(|devices| DeviceInventory {
            devices,
            warnings: Vec::new(),
        })
    }
    fn capture_targets(&self, _refresh: bool) -> Result<serde_json::Value, BackendError> {
        Err(BackendError::new(
            "listar aplicativos",
            std::io::Error::from(std::io::ErrorKind::Unsupported),
        ))
    }
    fn capture_configure(
        &mut self,
        _id: nodivu_core::graph::NodeId,
        _selection: Option<nodivu_core::capture::CaptureSelection>,
        _token: Option<&str>,
    ) -> Result<(), BackendError> {
        Err(BackendError::new(
            "selecionar aplicativo",
            std::io::Error::from(std::io::ErrorKind::Unsupported),
        ))
    }
    fn plugin_latency(&self, _id: &str) -> u32 {
        0
    }
    fn plugin_runtime(&self) -> serde_json::Value {
        serde_json::json!({})
    }
    fn plugin_command(
        &mut self,
        _id: nodivu_core::graph::NodeId,
        _action: &str,
        _path: Option<&str>,
        _position_ms: Option<u32>,
    ) -> Result<(), BackendError> {
        Err(BackendError::new(
            "controle de plugin",
            std::io::Error::from(std::io::ErrorKind::Unsupported),
        ))
    }
    fn plugin_catalog(&self) -> serde_json::Value {
        serde_json::json!([])
    }
    fn plugin_block(&self, _id: &str) -> Result<nodivu_core::graph::Block, BackendError> {
        Err(BackendError::new(
            "plugin indisponível",
            std::io::Error::from(std::io::ErrorKind::NotFound),
        ))
    }
    fn configure_plugins(&mut self, graph: &nodivu_core::graph::Graph) -> Result<(), BackendError> {
        self.validate_plugins(graph)
    }
    fn validate_plugins(&self, graph: &nodivu_core::graph::Graph) -> Result<(), BackendError> {
        if graph.nodes.iter().any(|n| n.block.kind() == "plugin") {
            return Err(BackendError::new(
                "plugin indisponível",
                std::io::Error::from(std::io::ErrorKind::Unsupported),
            ));
        }
        Ok(())
    }
    fn enumerate(&mut self) -> Result<Vec<DeviceInfo>, BackendError>;
    fn new_id(&mut self) -> Result<Uuid, BackendError>;
    fn start_audio(
        &mut self,
        _config: crate::AudioConfig,
    ) -> Result<crate::AudioSession, BackendError> {
        Err(BackendError::new(
            "abrir áudio neste backend",
            std::io::Error::from(std::io::ErrorKind::Unsupported),
        ))
    }
}
pub struct NativeBackend;
impl DeviceBackend for NativeBackend {
    fn device_inventory(&mut self) -> Result<DeviceInventory, BackendError> {
        #[cfg(windows)]
        {
            crate::windows_backend::inventory()
        }
        #[cfg(not(windows))]
        {
            self.enumerate().map(|devices| DeviceInventory {
                devices,
                warnings: Vec::new(),
            })
        }
    }
    #[cfg(windows)]
    fn start_audio(
        &mut self,
        config: crate::AudioConfig,
    ) -> Result<crate::AudioSession, BackendError> {
        crate::AudioSession::start(config)
    }
    fn enumerate(&mut self) -> Result<Vec<DeviceInfo>, BackendError> {
        #[cfg(windows)]
        {
            crate::windows_backend::enumerate()
        }
        #[cfg(not(windows))]
        {
            Err(BackendError::new(
                "Descoberta requer Windows nativo",
                std::io::Error::from(std::io::ErrorKind::Unsupported),
            ))
        }
    }
    fn new_id(&mut self) -> Result<Uuid, BackendError> {
        #[cfg(windows)]
        {
            crate::windows_backend::new_id()
        }
        #[cfg(not(windows))]
        {
            Err(BackendError::new(
                "IDs nativos requerem Windows",
                std::io::Error::from(std::io::ErrorKind::Unsupported),
            ))
        }
    }
}
