//! Ownership do worker e telemetria limitada; nenhum handle COM atravessa threads.
use crate::BackendError;
use nodivu_core::{ApiError, DeviceId, ErrorCode};
use serde_json::{Value, json};
use std::sync::{
    Arc,
    atomic::{AtomicBool, AtomicI32, AtomicU32, AtomicU64, Ordering},
};
use std::thread::JoinHandle;

pub struct AudioConfig {
    pub capture: Option<DeviceId>,
    pub output: DeviceId,
    pub plan: nodivu_core::graph::RenderPlan,
}

#[derive(Default)]
pub(crate) struct Shared {
    pub monitor: crate::monitor::Queue,
    pub route_nodes: [crate::diagnostics::SharedRouteNode; 32],
    pub route_latency: AtomicU32,
    pub route_delay: AtomicU32,
    pub stop: AtomicBool,
    pub state: AtomicU32, // 0 starting, 1 running, 2 error, 3 stopped
    pub plan: crate::plan_mailbox::PlanMailbox,
    pub failure: AtomicI32,
    pub capture_failure: AtomicI32,
    pub capture_failure_operation: AtomicU32,
    pub capture_stream_rate: AtomicU32,
    pub capture_bits: AtomicU32,
    pub capture_tag: AtomicU32,
    pub output_bits: AtomicU32,
    pub output_tag: AtomicU32,
    pub stage: AtomicU32,
    pub operation: AtomicU32,
    pub input_peak: AtomicU32,
    pub output_peak: AtomicU32,
    pub captured_frames: AtomicU64,
    pub rendered_frames: AtomicU64,
    pub underruns: AtomicU64,
    pub render_starvations: AtomicU64,
    pub overflows: AtomicU64,
    pub invalid_samples: AtomicU64,
    pub clipped_samples: AtomicU64,
    pub processor_errors: AtomicU64,
    pub external_timing: crate::diagnostics::SharedTiming,
    pub external_enabled: AtomicBool,
    pub external_input_peak: AtomicU32,
    pub external_output_peak: AtomicU32,
    pub timings: [crate::diagnostics::SharedTiming; crate::diagnostics::STAGES],
    pub nodes: [crate::diagnostics::SharedNode; nodivu_core::graph::MAX_GAIN_NODES],
    pub capture_periods: crate::diagnostics::DevicePeriods,
    pub output_periods: crate::diagnostics::DevicePeriods,
    pub capture_target_frames: AtomicU32,
    pub discontinuities: AtomicU64,
    pub occupancy: AtomicU32,
    pub capture_rate: AtomicU32,
    pub output_rate: AtomicU32,
    pub capture_channels: AtomicU32,
    pub output_channels: AtomicU32,
    pub capture_buffer_frames: AtomicU32,
    pub output_buffer_frames: AtomicU32,
    pub capture_period_100ns: AtomicU64,
    pub output_period_100ns: AtomicU64,
}

pub struct AudioSession {
    monitor: Option<crate::monitor::Monitor>,
    pub(crate) shared: Arc<Shared>,
    worker: Option<JoinHandle<()>>,
}
impl AudioSession {
    pub(crate) fn borrowed(shared: Arc<Shared>) -> Self {
        Self {
            shared,
            worker: None,
            monitor: None,
        }
    }
    #[cfg(windows)]
    pub fn start(config: AudioConfig) -> Result<Self, BackendError> {
        let (target_ms, capture_minimum) = audio_settings(&config)?;
        let shared = Arc::new(Shared::default());
        shared.plan.publish(config.plan);
        let worker_shared = Arc::clone(&shared);
        let worker = std::thread::Builder::new()
            .name("wasapi-monitor".into())
            .spawn(move || {
                let result =
                    crate::windows_audio::run(config, &worker_shared, target_ms, capture_minimum);
                // Recursos já foram liberados pelo worker antes de publicar o estado terminal.
                if let Err(error) = result {
                    worker_shared
                        .failure
                        .store(error.code().0, Ordering::Relaxed);
                    worker_shared.state.store(2, Ordering::Release);
                } else {
                    worker_shared.state.store(3, Ordering::Release);
                }
                worker_shared.input_peak.store(0, Ordering::Relaxed);
                worker_shared.output_peak.store(0, Ordering::Relaxed);
            })
            .map_err(|e| BackendError::new("criar worker de áudio", e))?;
        Ok(Self {
            shared,
            worker: Some(worker),
            monitor: None,
        })
    }
    pub fn set_monitor(&mut self, endpoint: Option<DeviceId>) -> Result<(), ApiError> {
        self.monitor = None;
        if let Some(id) = endpoint {
            self.monitor = Some(crate::monitor::Monitor::start(self.shared.clone(), id)?);
        }
        Ok(())
    }
    pub fn monitor_snapshot(&self) -> Value {
        self.monitor
            .as_ref()
            .map_or(json!({"state":"off"}), |m| m.snapshot())
    }
    pub fn state(&self) -> &'static str {
        if self.worker.as_ref().is_some_and(|w| w.is_finished())
            && self.shared.state.load(Ordering::Acquire) < 2
        {
            return "error";
        }
        match self.shared.state.load(Ordering::Acquire) {
            0 => "starting",
            1 => "running",
            2 => "error",
            _ => "stopped",
        }
    }
    pub fn set_amplitude(&mut self, amplitude: f32) -> Result<(), ApiError> {
        self.set_signal(nodivu_core::graph::SignalPlan {
            source: 1,
            amplitude,
        })
    }
    pub fn set_signal(&mut self, plan: nodivu_core::graph::SignalPlan) -> Result<(), ApiError> {
        self.set_render_plan(plan.into())
    }
    pub fn set_render_plan(
        &mut self,
        plan: nodivu_core::graph::RenderPlan,
    ) -> Result<(), ApiError> {
        plan.validate()?;
        self.shared.plan.publish(plan);
        Ok(())
    }
    pub fn error(&self) -> Option<ApiError> {
        if self.state() != "error" {
            return None;
        }
        Some(device_audio_error(
            &self.shared,
            self.shared.failure.load(Ordering::Relaxed),
            self.shared.stage.load(Ordering::Relaxed),
        ))
    }
    pub fn capture_error(&self) -> Option<ApiError> {
        let code = self.shared.capture_failure.load(Ordering::Acquire);
        (code != 0).then(|| device_audio_error(&self.shared, code, 1))
    }
    pub fn metrics(&self) -> Value {
        audio_metrics(&self.shared, self.state() == "running")
    }
    pub fn stop(&mut self) -> Result<(), BackendError> {
        self.monitor = None;
        self.shared.stop.store(true, Ordering::Relaxed);
        if self.worker.is_none() {
            return Ok(());
        }
        if let Some(worker) = self.worker.take() {
            worker.join().map_err(|_| {
                BackendError::new(
                    "encerrar worker de áudio",
                    std::io::Error::other("Worker terminou de forma inesperada"),
                )
            })?;
        }
        self.shared.state.store(3, Ordering::Release);
        Ok(())
    }
}

/// Experimento local por processo, sem alterar ambiente/padrões globais.
fn capture_target_ms() -> Result<u32, std::io::Error> {
    match std::env::var("NODIVU_CAPTURE_TARGET_MS") {
        Err(std::env::VarError::NotPresent) => Ok(20),
        Ok(value) if value == "20" => Ok(20),
        Ok(value) if value == "10" => Ok(10),
        Ok(value) if value == "15" => Ok(15),
        _ => Err(std::io::Error::new(
            std::io::ErrorKind::InvalidInput,
            "NODIVU_CAPTURE_TARGET_MS aceita 20 (padrão), 15 ou 10 (experimentais)",
        )),
    }
}
impl Drop for AudioSession {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

pub(crate) fn audio_error(code: i32, stage: u32) -> ApiError {
    let endpoint = if stage == 1 {
        "entrada"
    } else if stage == 2 {
        "saída"
    } else {
        "motor"
    };
    let (kind, hint) = match code as u32 {
        0x80070005 => (
            ErrorCode::PermissionDenied,
            "Verifique a permissão de microfone para aplicativos da área de trabalho no Windows.",
        ),
        0x88890004 | 0x80070490 => (
            ErrorCode::DeviceUnavailable,
            "O dispositivo foi removido ou reconfigurado. Atualize a lista e tente iniciar novamente.",
        ),
        0x88890008 => (
            ErrorCode::UnsupportedFormat,
            "O formato foi recusado pelo driver ou não é PCM/float compatível. Os detalhes abaixo identificam o formato e a etapa.",
        ),
        0x8889000a => (
            ErrorCode::DeviceBusy,
            "Outro aplicativo pode estar usando modo exclusivo. Libere o dispositivo e tente novamente.",
        ),
        _ => (
            ErrorCode::BackendError,
            "Pare a sessão, confira os dispositivos e tente novamente.",
        ),
    };
    ApiError::new(
        kind,
        format!("Falha na {endpoint} (WASAPI 0x{:08X}). {hint}", code as u32),
    )
}

pub(crate) fn audio_settings(config: &AudioConfig) -> Result<(u32, bool), BackendError> {
    config
        .plan
        .validate()
        .map_err(|e| BackendError::new("validar plano de áudio", e))?;
    let target_ms =
        capture_target_ms().map_err(|e| BackendError::new("configurar buffer de captura", e))?;
    let capture_minimum = match std::env::var("NODIVU_CAPTURE_PERIOD") {
        Err(std::env::VarError::NotPresent) => false,
        Ok(value) if value == "default" => false,
        Ok(value) if value == "minimum" => true,
        _ => {
            return Err(BackendError::new(
                "configurar período de captura",
                std::io::Error::new(
                    std::io::ErrorKind::InvalidInput,
                    "NODIVU_CAPTURE_PERIOD aceita default ou minimum (experimental)",
                ),
            ));
        }
    };
    Ok((target_ms, capture_minimum))
}
pub(crate) fn audio_metrics(s: &Shared, active: bool) -> Value {
    let capture_rate = s.capture_stream_rate.load(Ordering::Relaxed);
    let occupancy = s.occupancy.load(Ordering::Relaxed);
    let target = s.capture_target_frames.load(Ordering::Relaxed);
    let capture_ms = |frames: u32| {
        (capture_rate > 0).then(|| f64::from(frames) * 1000.0 / f64::from(capture_rate))
    };
    let stages: serde_json::Map<String, Value> = crate::diagnostics::NAMES
        .iter()
        .zip(&s.timings)
        .map(|(name, timing)| ((*name).into(), timing.snapshot()))
        .collect();
    json!({"route_nodes":s.route_nodes.iter().filter_map(|n| n.snapshot(active)).collect::<Vec<_>>(),"route_latency_frames":s.route_latency.load(Ordering::Relaxed),"compensation_frames":s.route_delay.load(Ordering::Relaxed),"input_peak": if active {f32::from_bits(s.input_peak.load(Ordering::Relaxed))} else {0.0},
            "backend_operation": s.operation.load(Ordering::Relaxed),
            "output_peak": if active {f32::from_bits(s.output_peak.load(Ordering::Relaxed))} else {0.0},
            "captured_frames": s.captured_frames.load(Ordering::Relaxed),
            "rendered_frames": s.rendered_frames.load(Ordering::Relaxed),
            "underruns": s.underruns.load(Ordering::Relaxed), "overflows": s.overflows.load(Ordering::Relaxed),
            "invalid_samples": s.invalid_samples.load(Ordering::Relaxed),
            "clipped_samples": s.clipped_samples.load(Ordering::Relaxed),
            "discontinuities": s.discontinuities.load(Ordering::Relaxed),
            "buffer_frames": s.occupancy.load(Ordering::Relaxed),
            "buffer_capacity_frames": capture_rate / 10,
            "buffer_target_frames": target,
            "processor_errors": s.processor_errors.load(Ordering::Relaxed),
            "latency_diagnostics": {
                "cpu_stages": stages,
                "snapshot_approximate": true,
                "render_total_includes_dsp_stages": true,
                "capture_queue_ms": capture_ms(occupancy),
                "capture_target_ms": capture_ms(target),
                "declared_processor_latency_frames": if s.external_enabled.load(Ordering::Relaxed) { None } else { Some(nodivu_plugin_gain::DESCRIPTOR.latency_frames) },
                "native_edge_buffering_frames": 0,
                "device_periods": { "capture":s.capture_periods.snapshot(), "output":s.output_periods.snapshot() },
                "physical_roundtrip_ms": Value::Null,
                "scope": if s.external_enabled.load(Ordering::Relaxed) { "ordered_native_chain" } else { "internal_chain" },
                "external_effect": {"cpu":s.external_timing.snapshot(), "input_peak": f32::from_bits(s.external_input_peak.load(Ordering::Relaxed)), "output_peak":f32::from_bits(s.external_output_peak.load(Ordering::Relaxed))},
                "nodes": s.nodes.iter().filter_map(|n| n.snapshot()).collect::<Vec<_>>()
            },
            "render_starvations": s.render_starvations.load(Ordering::Relaxed),
            "bus_rate_hz": crate::dsp::RATE,
            "capture_stream_rate_hz": capture_rate,
            "capture_bits_per_sample": s.capture_bits.load(Ordering::Relaxed),
            "output_bits_per_sample": s.output_bits.load(Ordering::Relaxed),
            "capture_format_tag": s.capture_tag.load(Ordering::Relaxed),
            "output_format_tag": s.output_tag.load(Ordering::Relaxed),
            "capture_failure_operation": s.capture_failure_operation.load(Ordering::Relaxed),
            "capture_mix_rate_hz": s.capture_rate.load(Ordering::Relaxed),
            "output_mix_rate_hz": s.output_rate.load(Ordering::Relaxed),
            "capture_channels": s.capture_channels.load(Ordering::Relaxed),
            "output_channels": s.output_channels.load(Ordering::Relaxed),
            "capture_buffer_frames": s.capture_buffer_frames.load(Ordering::Relaxed),
            "output_buffer_frames": s.output_buffer_frames.load(Ordering::Relaxed),
            "capture_period_100ns": s.capture_period_100ns.load(Ordering::Relaxed),
            "output_period_100ns": s.output_period_100ns.load(Ordering::Relaxed)})
}

/// Formatação somente no controle; worker publica apenas números atômicos.
pub(crate) fn device_audio_error(shared: &Shared, code: i32, stage: u32) -> ApiError {
    let mut error = audio_error(code, stage);
    let (rate, channels, bits, tag, operation) = if stage == 1 {
        (
            &shared.capture_rate,
            &shared.capture_channels,
            &shared.capture_bits,
            &shared.capture_tag,
            shared.capture_failure_operation.load(Ordering::Relaxed),
        )
    } else {
        (
            &shared.output_rate,
            &shared.output_channels,
            &shared.output_bits,
            &shared.output_tag,
            shared.operation.load(Ordering::Relaxed),
        )
    };
    let rate = rate.load(Ordering::Relaxed);
    let operation = match operation {
        1 => "localizar dispositivo",
        2 => "ativar dispositivo",
        3 => "consultar/validar formato",
        4 => "consultar período",
        5 => "negociar stream",
        6 => "registrar evento",
        7 => "consultar buffer",
        8 => "obter serviço",
        9 => "iniciar stream",
        10 => "transferir áudio",
        _ => "preparar motor",
    };
    error.message.push_str(&format!(" Etapa: {operation}."));
    if rate > 0 {
        error.message.push_str(&format!(
            " Formato do dispositivo: {rate} Hz, {} canal(is), {} bits, tag {}.",
            channels.load(Ordering::Relaxed),
            bits.load(Ordering::Relaxed),
            tag.load(Ordering::Relaxed)
        ));
    }
    error
}
