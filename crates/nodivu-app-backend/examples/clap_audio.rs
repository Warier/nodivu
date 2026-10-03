//! Ensaio nativo da fixture C: mesmos dispositivos/Bus WASAPI do app, owner CLAP no main.
//! Não é carregador de arquivos desconhecidos nem altera o projeto do Electron.
#[cfg(windows)]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    run::main()
}
#[cfg(not(windows))]
fn main() -> std::process::ExitCode {
    eprintln!("Este ensaio exige Windows nativo.");
    std::process::ExitCode::FAILURE
}

#[cfg(windows)]
mod run {
    use nodivu_block::{ParameterId, StereoMut};
    use nodivu_clap_host::{EffectControls, Error, LiveEffect, OfflinePlugin};
    use nodivu_core::{
        DeviceInfo, DeviceState, Flow,
        graph::{NodeId, RenderPlan, SignalPlan},
    };
    use nodivu_engine::{
        AudioConfig, DeviceBackend, NativeBackend, external_audio::ExternalAudioRun,
    };
    use serde_json::{Value, json};
    use std::{
        path::Path,
        sync::atomic::{AtomicBool, Ordering::Relaxed},
        time::{Duration, Instant},
    };
    type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

    fn endpoint(devices: &[DeviceInfo], flow: Flow, variable: &str) -> Result<DeviceInfo> {
        let selected = std::env::var(variable).ok();
        devices
            .iter()
            .find(|d| {
                d.flow == flow
                    && d.state == DeviceState::Active
                    && selected
                        .as_ref()
                        .map_or(d.is_default, |id| d.endpoint_id.0 == *id)
            })
            .cloned()
            .ok_or_else(|| {
                format!("Endpoint ativo/padrão ausente; selecione ID exato em {variable}").into()
            })
    }

    pub fn main() -> Result<()> {
        let args: Vec<_> = std::env::args().skip(1).collect();
        if args.len() < 4 || args.len() > 5 {
            return Err("Uso: clap_audio <fixture.clap> <escala 0.5|0.25> <tone|capture|separate> <3..60 segundos> [audible]".into());
        }
        let scale: f64 = args[1].parse()?;
        if ![0.5, 0.25].contains(&scale) {
            return Err("Escala deve ser 0.5 ou 0.25".into());
        }
        let capture = args[2] == "capture";
        let separate = args[2] == "separate";
        let negative = ["process-fail", "nan", "restart"].contains(&args[2].as_str());
        if !capture && !separate && args[2] != "tone" && !negative {
            return Err("Fonte desconhecida".into());
        }
        let seconds: u32 = args[3].parse()?;
        if !(3..=60).contains(&seconds) {
            return Err("Duração fora de 3..60 segundos".into());
        }
        let audible = match args.get(4).map(String::as_str) {
            None => false,
            Some("audible") => true,
            _ => return Err("Opção desconhecida: use audible ou omita".into()),
        };
        let devices = NativeBackend.enumerate()?;
        let output = endpoint(&devices, Flow::Render, "NODIVU_TEST_OUTPUT_ID")?;
        let input = if capture {
            Some(endpoint(&devices, Flow::Capture, "NODIVU_TEST_CAPTURE_ID")?)
        } else {
            None
        };
        let kind = if negative {
            args[2].as_str()
        } else if separate {
            "separate"
        } else {
            "gain"
        };
        // SAFETY: ferramenta dedicada à fixture C confiável do repositório, no main do processo.
        // O escopo recolhe o único worker antes de desativar/destruir/descarregar a DLL.
        let mut plugin = unsafe {
            OfflinePlugin::load(Path::new(&args[0]), &format!("org.nodivu.fixture.{kind}"))
        }?;
        if !plugin.metadata().has_input || plugin.metadata().latency_frames != 0 {
            return Err("Este teste de bypass exige efeito sem latência declarada".into());
        }
        let effect_controls = EffectControls::prepare(plugin.metadata())?;
        let (audio, control) = ExternalAudioRun::prepare(AudioConfig {
            capture: input.as_ref().map(|d| d.endpoint_id.clone()),
            output: output.endpoint_id.clone(),
            plan: {
                let mut plan = RenderPlan::from(SignalPlan {
                    source: if capture { 1 } else { 2 },
                    amplitude: if audible { 0.015_848_933 } else { 0.0 },
                });
                plan.external[0] = Some(NodeId(uuid::Uuid::nil()));
                plan.order[0] = 8;
                plan.count = 1;
                plan
            },
        })?;
        let failed = AtomicBool::new(false);
        let mut samples = Vec::with_capacity(seconds as usize * 12);
        let mut started = None;
        let waiting = Instant::now();
        let mut sampled = Instant::now();
        let mut last_phase = 0;
        let mut capture_error = None;
        let ((audio_result, plugin_error, adapter_stats), callbacks) = plugin
            .with_audio_worker_control(
                |processor| {
                    let mut effect = LiveEffect::prepare(processor, &effect_controls)?;
                    let mut first_error = None;
                    let result = audio.run(&mut |block: StereoMut<'_>, _| {
                        if first_error.is_some() {
                            return false;
                        }
                        let processed = effect.process(block);
                        if let Err(error) = processed {
                            // Causa movida, nunca formatada/alocada no callback. Main pede stop.
                            first_error = Some(error);
                            failed.store(true, Relaxed);
                            control.request_stop();
                            return false;
                        }
                        true
                    });
                    Ok((result, first_error, effect.stats()))
                },
                || {
                    if capture_error.is_none() {
                        capture_error = control.capture_error();
                    }
                    if capture_error.is_some() {
                        control.request_stop();
                    }
                    if failed.load(Relaxed) {
                        control.request_stop();
                    }
                    if control.running() && started.is_none() {
                        started = Some(Instant::now());
                    }
                    if let Some(start) = started {
                        let elapsed = start.elapsed().as_secs_f64();
                        let phase = ((elapsed * 3.0 / f64::from(seconds)) as usize).min(2);
                        if phase != last_phase {
                            effect_controls.set_parameter(ParameterId(7), 0.25)?;
                            effect_controls.set_bypass(phase == 2);
                            last_phase = phase;
                        }
                        if sampled.elapsed() >= Duration::from_millis(100) {
                            samples.push(
                            json!({"seconds":elapsed, "phase":phase, "metrics":control.metrics()}),
                        );
                            sampled = Instant::now();
                        }
                        if elapsed >= f64::from(seconds) {
                            control.request_stop();
                        }
                    } else if waiting.elapsed() > Duration::from_secs(5) {
                        control.request_stop();
                        return Err(Error::Contract("WASAPI não iniciou em cinco segundos"));
                    }
                    Ok(())
                },
            )?;
        audio_result?;
        if let Some(error) = capture_error {
            return Err(error.into());
        }
        if let Some(error) = plugin_error {
            return Err(error.into());
        }
        let final_metrics = control.metrics();
        let checks = verify(&samples, seconds, scale)?;
        if final_metrics["processor_errors"] != 0
            || final_metrics["rendered_frames"]
                .as_u64()
                .is_none_or(|n| n == 0)
        {
            return Err("Processamento falhou ou não renderizou frames".into());
        }
        let metadata = serde_json::to_value(plugin.metadata())?;
        println!(
            "{}",
            serde_json::to_string_pretty(&json!({
                "kind":"clap_wasapi_fixture", "source":args[2], "audible":audible,
                "master_db":if audible {Some(-36)} else {None}, "capture":input,"output":output,
                "plugin":metadata,"adapter_stats":adapter_stats,"main_callbacks":callbacks,"phase_checks":checks,
                "metrics":final_metrics,"samples":samples,
                "physical_roundtrip_ms":Value::Null,"canvas_integration":false
            }))?
        );
        Ok(())
    }
    fn verify(samples: &[Value], seconds: u32, scale: f64) -> Result<Value> {
        let mut checks = Vec::new();
        for (phase, expected) in [scale, scale * 0.25, 1.0].into_iter().enumerate() {
            let mut count = 0;
            let mut max_error = 0.0_f64;
            for sample in samples {
                let elapsed = sample["seconds"].as_f64().ok_or("Tempo ausente")?;
                if sample["phase"] != phase
                    || elapsed < f64::from(seconds) * phase as f64 / 3.0 + 0.4
                {
                    continue;
                }
                let signal = &sample["metrics"]["latency_diagnostics"]["external_effect"];
                let input = signal["input_peak"]
                    .as_f64()
                    .ok_or("Pico de entrada ausente")?;
                if input < 1e-7 {
                    continue;
                }
                let output = signal["output_peak"]
                    .as_f64()
                    .ok_or("Pico de saída ausente")?;
                max_error = max_error.max((output / input - expected).abs());
                count += 1;
            }
            if count < 2 || max_error > 0.02 {
                return Err(format!("Fase {phase} sem sinal suficiente ou razão incorreta: {count} amostras, erro {max_error}").into());
            }
            checks.push(json!({"phase":phase,"expected_ratio":expected,"samples":count,"max_ratio_error":max_error}));
        }
        Ok(json!(checks))
    }
}
