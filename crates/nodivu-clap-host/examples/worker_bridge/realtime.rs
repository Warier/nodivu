//! Clocked callback simulation with the real Python process; no WASAPI/device.
use super::broker::Broker;
use nodivu_clap_host::worker_audio::{FRAMES, LATENCY_FRAMES, prepare};
use serde_json::{Value, json};
use std::{
    path::Path,
    sync::{
        atomic::{AtomicBool, Ordering},
        mpsc,
    },
    time::{Duration, Instant},
};

pub fn run(python: &str, script: &Path) -> Result<Value, String> {
    let (mut audio, mut port) = prepare();
    let stop = AtomicBool::new(false);
    let (ready, startup) = mpsc::sync_channel(1);
    std::thread::scope(|scope| {
        let service = std::thread::Builder::new()
            .name("python-audio-service".into())
            .spawn_scoped(scope, || -> Result<(), String> {
                // Mapping and child ownership stay on this service thread throughout.
                let mut broker = match Broker::start_for_test(python, script) {
                    Ok(broker) => broker,
                    Err(error) => {
                        let message = error.to_string();
                        let _ = ready.send(Err(message.clone()));
                        return Err(message);
                    }
                };
                ready.send(Ok(())).map_err(|e| e.to_string())?;
                let mut completed = 0;
                while !stop.load(Ordering::Relaxed) {
                    let Some(mut item) = port.receive() else {
                        std::thread::sleep(Duration::from_millis(1));
                        continue;
                    };
                    if completed == 32 {
                        // The broker blocks here for its deadline; audio must keep running.
                        if broker
                            .request(
                                json!({"op":"test_fault","fault":"stall"}),
                                Duration::from_millis(100),
                            )
                            .is_ok()
                        {
                            return Err("stall unexpectedly succeeded".into());
                        }
                        return Ok(()); // Deliberate worker failure; no auto-restart.
                    }
                    let (_, output) = broker
                        .process(&item.samples, FRAMES, "effect", 0.5)
                        .map_err(|e| e.to_string())?;
                    item.samples = output;
                    port.complete(item);
                    completed += 1;
                }
                Err("clock stopped before fault injection".into())
            })
            .map_err(|e| e.to_string())?;
        // All waiting, error formatting and thread teardown are outside process().
        let result = (|| {
            startup
                .recv_timeout(Duration::from_secs(6))
                .map_err(|e| e.to_string())??;
            let mut cpu = Vec::with_capacity(100);
            let mut rendered = 0;
            let mut silent_tail = 0;
            for tick in 0..100 {
                let mut samples = [[0.125; FRAMES]; 2];
                let start = Instant::now();
                audio
                    .process(&mut samples, FRAMES)
                    .map_err(|e| format!("audio: {e:?}"))?;
                cpu.push(start.elapsed().as_secs_f64() * 1e6);
                if samples[0].iter().all(|v| *v == 0.0625) {
                    rendered += 1;
                }
                if tick >= 80 && samples.iter().flatten().all(|v| *v == 0.0) {
                    silent_tail += 1;
                }
                // Simulated device pacing, deliberately outside measured processing.
                std::thread::sleep(Duration::from_millis(10));
            }
            if rendered < 10 || silent_tail != 20 {
                return Err(format!(
                    "signal/fallback failed: rendered={rendered}, tail={silent_tail}"
                ));
            }
            cpu.sort_by(f64::total_cmp);
            let d = audio.diagnostics();
            Ok(
                json!({"status":"PASS","scope":"clocked callback simulation, real Python; no WASAPI", "latency_frames":LATENCY_FRAMES,
                "callbacks":100,"rendered_blocks":rendered,"silent_final_blocks":silent_tail,
                "callback_us":{"p50":cpu[50],"p99":cpu[99]},
                "missing_blocks":d.missing_blocks,"input_overflows":d.input_overflows,"discarded_responses":d.discarded_responses,"rejected_completions":d.rejected_completions}),
            )
        })();
        stop.store(true, Ordering::Relaxed);
        let worker_result = service
            .join()
            .map_err(|_| "service thread panicked".to_owned())?;
        result.and_then(|value| worker_result.map(|()| value))
    })
}
