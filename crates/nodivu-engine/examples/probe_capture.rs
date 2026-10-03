//! Ensaio opt-in de hardware. IDs explícitos; não muda volume, formato ou padrão.
//! probe_capture <capture-id> <output-id> [--expect-capture-failure <cable-capture-id>]
//! Sem a opção de falha, lê o mic e escreve silêncio. Com ela, envia tom ao cabo
//! e verifica a chegada em outro cliente WASAPI, apesar do mic indisponível.
#[cfg(windows)]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    use nodivu_core::{DeviceId, graph::SignalPlan};
    use nodivu_engine::{AudioConfig, AudioSession};
    use std::time::{Duration, Instant};
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 2 && !(args.len() == 4 && args[2] == "--expect-capture-failure") {
        return Err("capture-id output-id [--expect-capture-failure cable-capture-id]".into());
    }
    let failure = args.len() == 4;
    let mut receiver = if failure {
        Some(AudioSession::start(AudioConfig {
            capture: Some(DeviceId(args[3].clone())),
            output: DeviceId(args[1].clone()),
            plan: SignalPlan {
                source: 1,
                amplitude: 0.0,
            }
            .into(),
        })?)
    } else {
        None
    };
    let mut audio = AudioSession::start(AudioConfig {
        capture: Some(DeviceId(args[0].clone())),
        output: DeviceId(args[1].clone()),
        plan: SignalPlan {
            source: if failure { 2 } else { 1 },
            amplitude: if failure { 0.1 } else { 0.0 },
        }
        .into(),
    })?;
    let deadline = Instant::now() + Duration::from_secs(5);
    let mut received = 0.0_f64;
    while Instant::now() < deadline {
        if let Some(error) = audio.error() {
            return Err(error.message.into());
        }
        if let Some(rx) = &receiver {
            if let Some(error) = rx.error().or_else(|| rx.capture_error()) {
                return Err(error.message.into());
            }
            received = received.max(rx.metrics()["input_peak"].as_f64().unwrap_or(0.0));
        }
        std::thread::sleep(Duration::from_millis(100));
    }
    let metrics = audio.metrics();
    println!(
        "{}",
        serde_json::json!({"state": audio.state(), "capture_error": audio.capture_error(), "metrics": metrics, "received_peak": received})
    );
    if audio.state() != "running" || audio.capture_error().is_some() != failure {
        return Err("Estado/isolamento da captura inesperado".into());
    }
    if failure {
        if received < 0.001 {
            return Err("Tom não chegou ao receptor com mic indisponível".into());
        }
    } else if metrics["captured_frames"].as_u64().unwrap_or(0) == 0 {
        return Err("Nenhum frame capturado".into());
    }
    audio.stop()?;
    if let Some(rx) = &mut receiver {
        rx.stop()?;
    }
    Ok(())
}
#[cfg(not(windows))]
fn main() {
    eprintln!("Este ensaio exige WASAPI/Windows e endpoints explícitos.");
    std::process::exit(1);
}
