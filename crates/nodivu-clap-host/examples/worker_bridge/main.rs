//! Cross-language transport/correctness benchmark; no WASAPI and no model download.
#[cfg(windows)]
mod broker {
    pub use nodivu_clap_host::worker_process::Broker;
}
#[cfg(windows)]
mod realtime;
#[cfg(windows)]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    use broker::Broker;
    use serde_json::json;
    use std::{
        path::Path,
        time::{Duration, Instant},
    };
    let args: Vec<_> = std::env::args().skip(1).collect();
    if args.len() != 2 {
        return Err("Usage: worker_bridge <python.exe> <worker.py>".into());
    }
    let script = Path::new(&args[1]).canonicalize()?;
    let mut peer = Broker::start_for_test(&args[0], &script)?;
    let mut input = [[0.0; 480]; 2];
    input[0][17] = 0.8;
    input[1][17] = -0.4;
    let (_, output) = peer.process(&input, 64, "effect", 0.5)?;
    if output[0][17] != 0.4
        || output[1][17] != -0.2
        || output[0][..64].iter().filter(|v| **v != 0.0).count() != 1
    {
        return Err("gain/impulse/channel isolation failed".into());
    }
    peer.request(json!({"op":"begin","frames":481}), Duration::from_secs(1))?;
    let (first, out) = peer.process(&[[0.0; 480]; 2], 480, "source", 1.0)?;
    if first["produced"] != 480 || first["done"] != false || !out[0].iter().any(|v| v.abs() > 0.01)
    {
        return Err("source generation failed".into());
    }
    let (last, out) = peer.process(&input, 64, "source", 1.0)?;
    if last["produced"] != 1 || last["done"] != true || out[0][1..64].iter().any(|v| *v != 0.0) {
        return Err("EOF padding failed".into());
    }
    peer.request(json!({"op":"reset"}), Duration::from_secs(1))?;
    let (reset, output) = peer.process(&input, 480, "source", 1.0)?;
    if reset["produced"] != 0 || reset["done"] != true || output.iter().flatten().any(|v| *v != 0.0)
    {
        return Err("reset retained previous generation".into());
    }
    let mut invalid = input;
    invalid[1][1] = f32::NAN;
    if peer.process(&invalid, 64, "effect", 1.0).is_ok()
        || peer.process(&input, 481, "effect", 1.0).is_ok()
    {
        return Err("invalid PCM accepted".into());
    }
    let mut timings = Vec::new();
    let mut processing = Vec::new();
    for i in 0..550 {
        let start = Instant::now();
        let (reply, _) = peer.process(&input, 480, "effect", 1.0)?;
        if i >= 50 {
            timings.push(start.elapsed().as_secs_f64() * 1e6);
            processing.push(reply["processing_us"].as_f64().ok_or("timing missing")?);
        }
    }
    timings.sort_by(f64::total_cmp);
    processing.sort_by(f64::total_cmp);
    peer.request(json!({"op":"close"}), Duration::from_secs(1))?;
    drop(peer);
    for fault in ["exit", "stall", "old_reply", "wrong_epoch", "oversize"] {
        let mut peer = Broker::start_for_test(&args[0], &script)?;
        if peer
            .request(
                json!({"op":"test_fault","fault":fault}),
                Duration::from_millis(100),
            )
            .is_ok()
        {
            return Err(format!("fault accepted: {fault}").into());
        }
        if peer.process(&input, 64, "effect", 1.0).is_ok() {
            return Err("failed epoch reused".into());
        }
    }
    let realtime = realtime::run(&args[0], &script)?;
    println!(
        "{}",
        serde_json::to_string_pretty(
            &json!({"status":"PASS","samples":500,"frames":480,"roundtrip_us":{"p50":timings[250],"p95":timings[475],"p99":timings[495],"max":timings[499]},"python_dsp_us_p50":processing[250],"verified":["effect","source","EOF","reset","invalid_PCM","process_exit","timeout","stale_reply","wrong_epoch","bounded_reply"],"scope":"blocking broker prototype; not acoustic latency", "realtime_boundary":realtime})
        )?
    );
    Ok(())
}
#[cfg(not(windows))]
fn main() {
    eprintln!("worker_bridge requires native Windows");
    std::process::exit(1);
}
