//! Controlador de sessão; backends nativos são adaptadores privados.
pub mod app;
mod audio;
pub mod diagnostics;
pub mod discovery;
mod dsp;
pub mod external_audio;
mod live_graph;
mod pipeline;
mod plan_mailbox;
pub mod routing_preview;
#[cfg(windows)]
mod windows_audio;
#[cfg(windows)]
mod windows_backend;

pub use audio::{AudioConfig, AudioSession};
pub use discovery::{BackendError, DeviceBackend, NativeBackend};
