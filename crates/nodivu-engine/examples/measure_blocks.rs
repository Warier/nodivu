//! Harness offline: custo por instância, overhead do relógio e atraso por impulso.
//! Não mede DLL/ABI C, driver ou roundtrip físico.
use nodivu_block::{PreparedEffect, ProcessSpec, StereoMut};
use nodivu_plugin_gain::Gain;
use serde_json::json;
use std::{hint::black_box, time::Instant};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut reports = Vec::new();
    for frames in [64, 128, 480] {
        for count in [0, 1, 8, 32] {
            let spec = ProcessSpec::new(48_000, frames)?;
            let mut chain: Vec<Box<dyn PreparedEffect>> = (0..count)
                .map(|_| Gain::prepare(spec, 0.5).map(|g| Box::new(g) as Box<dyn PreparedEffect>))
                .collect::<Result<_, _>>()?;
            let mut left = vec![0.0; frames];
            let mut right = vec![0.0; frames];
            left[17] = 1.0;
            right[17] = -1.0;
            for effect in &mut chain {
                effect.process(StereoMut::new(&mut left, &mut right), &[])?;
            }
            let first = left
                .iter()
                .position(|v| *v != 0.0)
                .ok_or("Impulso desapareceu")?;
            let expected = 0.5_f32.powi(count);
            if first != 17
                || left[17] != expected
                || right[17] != -expected
                || left.iter().filter(|v| **v != 0.0).count() != 1
            {
                return Err("Cadeia alterou posição/conteúdo esperado do impulso".into());
            }
            let mut per_node = vec![0_u128; count as usize];
            let mut samples = Vec::with_capacity(10_000);
            // Preparação/fill/sort fora da medição; aquecimento não entra nos percentis.
            for iteration in 0..11_000 {
                left.fill(1.0);
                right.fill(-1.0);
                let started = Instant::now();
                for (index, effect) in chain.iter_mut().enumerate() {
                    let node_started = Instant::now();
                    black_box(effect).process(
                        StereoMut::new(black_box(&mut left), black_box(&mut right)),
                        &[],
                    )?;
                    let elapsed = node_started.elapsed().as_nanos();
                    if iteration >= 1000 {
                        per_node[index] += elapsed;
                    }
                }
                let elapsed = started.elapsed().as_nanos();
                black_box(&left);
                black_box(&right);
                if iteration >= 1000 {
                    samples.push(elapsed);
                }
            }
            samples.sort_unstable();
            reports.push(json!({"frames": frames, "nodes": count, "added_impulse_frames": first - 17,
                "declared_latency_frames": chain.iter().map(|g| g.descriptor().latency_frames).sum::<u32>(),
                "p50_us": samples[4999] as f64 / 1000.0, "p95_us": samples[9499] as f64 / 1000.0,
                "p999_us": samples[9989] as f64 / 1000.0, "max_us": samples[9999] as f64 / 1000.0,
                "node_mean_us": per_node.iter().map(|n| *n as f64 / 10_000.0 / 1000.0).collect::<Vec<_>>() }));
        }
    }
    println!(
        "{}",
        serde_json::to_string_pretty(&json!({
            "scope": "offline Rust calls, not CLAP/DLL/hardware", "samples_per_case": 10000,
            "timers_included": true, "zero_nodes_is_timer_baseline": true, "cases": reports
        }))?
    );
    Ok(())
}
