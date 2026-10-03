//! Adaptador CLAP experimental. A entrada unsafe exige um binário confiável e conforme à ABI.
//! Owner não é Send/Sync; DSP pode ser emprestado exclusivamente a um worker com lifetime delimitado.
#[cfg(windows)]
mod live_effect;
#[cfg(windows)]
mod native;
pub mod worker_audio;
#[cfg(windows)]
pub mod worker_process;
#[cfg(windows)]
pub use live_effect::{EffectControls, LiveEffect, LiveEffectStats};
#[cfg(windows)]
pub use native::{
    AudioProcessor, CaptureState, CaptureTarget, Error, Metadata, OfflinePlugin, ParameterInfo,
    ProcessStatus,
};

/// Atende no máximo 64 mensagens nativas, fora de process; false indica WM_QUIT.
/// Deve ser chamado na thread principal do processo, regularmente inclusive sem IPC.
#[cfg(windows)]
pub fn pump_main_thread() -> bool {
    use windows::Win32::UI::WindowsAndMessaging::*;
    for _ in 0..64 {
        let mut message = MSG::default();
        // SAFETY: MSG válido e fila da thread chamadora; não retém ponteiros após o dispatch.
        unsafe {
            if !PeekMessageW(&mut message, None, 0, 0, PM_REMOVE).as_bool() {
                break;
            }
            if message.message == WM_QUIT {
                return false;
            }
            let _ = TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    true
}
