//! Execução WASAPI na thread emprestada pelo host de plugins, sem criar outro worker.
//! O chamador mantém owner/callbacks vivos e faz join antes de destruir o plugin.
use crate::{
    AudioConfig, BackendError,
    audio::{Shared, audio_metrics, audio_settings},
};
use nodivu_block::StereoMut;
use nodivu_core::{ApiError, graph::RenderPlan};
use serde_json::Value;
use std::sync::{Arc, atomic::Ordering};

/// Callback planar limitado, sem reter buffers. false silencia; o adaptador guarda a causa.
pub type StereoEffect<'a> =
    dyn for<'b> FnMut(StereoMut<'b>, Option<nodivu_core::graph::NodeId>) -> bool + 'a;

pub struct ExternalAudioRun {
    config: AudioConfig,
    shared: Arc<Shared>,
    target_ms: u32,
    minimum: bool,
}
/// Único escritor do plano. Stop apenas solicita: observar finished/join antes de liberar recursos.
pub struct ExternalAudioControl {
    shared: Arc<Shared>,
    stop_on_drop: bool,
}
impl ExternalAudioRun {
    pub fn stop_callback(&self) -> impl Fn() + Send + 'static {
        let shared = self.shared.clone();
        move || {
            shared.stop.store(true, Ordering::Relaxed);
        }
    }
    /// Sessão controlada pelo app; o owner externo recolhe o worker no encerramento do processo.
    pub fn prepare_session(
        config: AudioConfig,
    ) -> Result<(Self, crate::AudioSession), BackendError> {
        let (run, mut control) = Self::prepare(config)?;
        let session = crate::AudioSession::borrowed(control.shared.clone());
        // Control::drop solicita stop; aqui a posse do controle é transferida à sessão.
        control.stop_on_drop = false;
        Ok((run, session))
    }
    pub fn prepare(config: AudioConfig) -> Result<(Self, ExternalAudioControl), BackendError> {
        let (target_ms, minimum) = audio_settings(&config)?;
        let shared = Arc::new(Shared::default());
        shared.external_enabled.store(true, Ordering::Relaxed);
        shared.plan.publish(config.plan);
        Ok((
            Self {
                config,
                shared: shared.clone(),
                target_ms,
                minimum,
            },
            ExternalAudioControl {
                shared,
                stop_on_drop: true,
            },
        ))
    }
    #[cfg(windows)]
    pub fn run(self, effect: &mut StereoEffect<'_>) -> Result<(), BackendError> {
        struct Completion<'a>(&'a Shared);
        impl Drop for Completion<'_> {
            fn drop(&mut self) {
                // Também publica falha se um panic do worker interromper o corpo (sem capturar FFI).
                if self.0.state.load(Ordering::Acquire) < 2 {
                    self.0.state.store(2, Ordering::Release);
                }
                self.0.input_peak.store(0, Ordering::Relaxed);
                self.0.output_peak.store(0, Ordering::Relaxed);
            }
        }
        let _completion = Completion(&self.shared);
        let result = crate::windows_audio::run_with_effect(
            self.config,
            &self.shared,
            self.target_ms,
            self.minimum,
            Some(effect),
        );
        if let Err(error) = &result {
            self.shared.failure.store(error.code().0, Ordering::Relaxed);
            self.shared.state.store(2, Ordering::Release);
        } else {
            self.shared.state.store(3, Ordering::Release);
        }
        result.map_err(|e| BackendError::new("áudio WASAPI com processador externo", e))
    }
}
impl ExternalAudioControl {
    pub fn set_plan(&mut self, plan: RenderPlan) -> Result<(), ApiError> {
        plan.validate()?;
        self.shared.plan.publish(plan);
        Ok(())
    }
    pub fn request_stop(&self) {
        self.shared.stop.store(true, Ordering::Relaxed);
    }
    pub fn finished(&self) -> bool {
        self.shared.state.load(Ordering::Acquire) >= 2
    }
    pub fn running(&self) -> bool {
        self.shared.state.load(Ordering::Acquire) == 1
    }
    pub fn metrics(&self) -> Value {
        audio_metrics(&self.shared, self.running())
    }
    /// Consultar no controle, fora do DSP: constrói a mensagem preservando o HRESULT.
    pub fn capture_error(&self) -> Option<ApiError> {
        let code = self.shared.capture_failure.load(Ordering::Acquire);
        (code != 0).then(|| crate::audio::device_audio_error(&self.shared, code, 1))
    }
}
impl Drop for ExternalAudioControl {
    fn drop(&mut self) {
        if self.stop_on_drop {
            self.request_stop();
        }
    }
}
