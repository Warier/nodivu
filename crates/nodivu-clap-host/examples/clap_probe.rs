//! Harness explícito para a fixture confiável examples/clap-gain, sem áudio/hardware.
//! Carrega código nativo: não é scanner seguro para arquivos desconhecidos.
#[cfg(windows)]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    run::main()
}
#[cfg(not(windows))]
fn main() -> std::process::ExitCode {
    eprintln!("Este harness exige Windows nativo.");
    std::process::ExitCode::FAILURE
}

#[cfg(windows)]
mod run {
    use nodivu_block::{ParameterEvent, ParameterId, StereoMut};
    use nodivu_clap_host::{EffectControls, Error, LiveEffect, OfflinePlugin};
    use serde_json::json;
    use std::{path::Path, time::Instant};
    type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;

    fn open(path: &Path, kind: &str) -> Result<OfflinePlugin> {
        // SAFETY: esta ferramenta aceita explicitamente só a fixture confiável do repositório;
        // chamada pelo main do processo; sem compartilhamento, todas as instâncias são locais.
        Ok(unsafe { OfflinePlugin::load(path, &format!("org.nodivu.fixture.{kind}")) }?)
    }
    fn ready(path: &Path, kind: &str) -> Result<OfflinePlugin> {
        let mut p = open(path, kind)?;
        p.activate()?;
        p.service_main_thread()?;
        Ok(p)
    }
    fn verify_live_effect(path: &Path, scale: f32) -> Result<serde_json::Value> {
        let mut reports = Vec::new();
        for kind in ["gain", "separate"] {
            let mut plugin = open(path, kind)?;
            let controls = EffectControls::prepare(plugin.metadata())?;
            let (stats, _) = plugin.with_audio_worker(|processor| {
                let mut effect = LiveEffect::prepare(processor, &controls)?;
                let mut l = [1.0; 480];
                let mut r = [-1.0; 480];
                effect.process(StereoMut::new(&mut l[..64], &mut r[..64]))?;
                assert_eq!(l[63], scale);
                assert!(
                    effect
                        .process(StereoMut::new(&mut l[..0], &mut r[..0]))
                        .is_err()
                );
                controls.set_parameter(ParameterId(7), 0.25)?;
                assert!(controls.set_parameter(ParameterId(7), f64::NAN).is_err());
                assert!(controls.set_parameter(ParameterId(999), 0.5).is_err());
                assert_eq!(controls.parameter_value(ParameterId(7)), Some(0.25));
                l.fill(1.0);
                r.fill(-1.0);
                effect.process(StereoMut::new(&mut l[..128], &mut r[..128]))?;
                assert_eq!(l[127], scale * 0.25);
                if kind == "gain" {
                    assert_eq!(effect.stats().dry_copy_frames, 0);
                }
                controls.set_bypass(true);
                let mut previous = scale * 0.25;
                for frames in [64, 128, 288] {
                    l.fill(1.0);
                    r.fill(-1.0);
                    effect.process(StereoMut::new(&mut l[..frames], &mut r[..frames]))?;
                    for i in 0..frames {
                        assert!(l[i] >= previous && l[i] <= 1.0);
                        assert_eq!(r[i], -l[i]);
                        previous = l[i];
                    }
                }
                assert_eq!(previous, 1.0); // Fim exato em 480 frames, independente da segmentação.
                l.fill(1.0);
                r.fill(-1.0);
                effect.process(StereoMut::new(&mut l[..64], &mut r[..64]))?;
                assert_eq!(l[..64], [1.0; 64]);
                controls.set_bypass(false);
                l.fill(1.0);
                r.fill(-1.0);
                effect.process(StereoMut::new(&mut l, &mut r))?;
                assert!(l.windows(2).all(|p| p[1] <= p[0]));
                assert_eq!(l[479], scale * 0.25);
                l.fill(1.0);
                r.fill(-1.0);
                effect.process(StereoMut::new(&mut l[..64], &mut r[..64]))?;
                let stats = effect.stats();
                assert_eq!(stats.calls, 8);
                assert_eq!(stats.parameter_events, 2);
                assert_eq!(stats.mixed_frames, 1024);
                assert_eq!(
                    stats.dry_copy_frames,
                    if kind == "gain" { 1024 } else { 1280 }
                );
                Ok(stats)
            })?;
            reports.push(json!({"kind":kind,"stats":stats}));
        }
        Ok(json!(reports))
    }
    fn verify(path: &Path, scale: f32) -> Result<serde_json::Value> {
        let live_effect = verify_live_effect(path, scale)?;
        let mut gain = ready(path, "gain")?;
        let mut other = ready(path, "gain")?;
        let mut l = [1.0; 128];
        let mut r = [-1.0; 128];
        let event = ParameterEvent {
            frame_offset: 64,
            id: ParameterId(7),
            value: 0.25,
        };
        gain.process_in_place(StereoMut::new(&mut l, &mut r), &[event])?;
        gain.service_main_thread()?;
        assert_eq!(l[0], scale);
        assert_eq!(l[63], scale);
        assert_eq!(l[64], scale * 0.25);
        assert_eq!(r[127], -scale * 0.25);
        other.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
        other.service_main_thread()?;
        assert_eq!(l[0], scale * scale); // Outra instância não herdou o parâmetro.
        assert_eq!(gain.metadata().latency_frames, 0);
        // Lote inválido rejeitado inteiro, sem modificar buffers nem parâmetro da instância.
        let before = l;
        assert!(
            gain.process_in_place(
                StereoMut::new(&mut l, &mut r),
                &[
                    ParameterEvent {
                        frame_offset: 0,
                        value: 1.0,
                        ..event
                    },
                    ParameterEvent {
                        frame_offset: 128,
                        ..event
                    },
                ]
            )
            .is_err()
        );
        assert_eq!(l, before);
        for invalid in [
            ParameterEvent {
                id: ParameterId(999),
                ..event
            },
            ParameterEvent {
                value: f64::NAN,
                ..event
            },
            ParameterEvent {
                value: 1.1,
                ..event
            },
        ] {
            assert!(
                gain.process_in_place(StereoMut::new(&mut l, &mut r), &[invalid])
                    .is_err()
            );
            assert_eq!(l, before);
        }
        assert!(
            gain.process_in_place(StereoMut::new(&mut l, &mut r), &[event; 33])
                .is_err()
        );
        assert!(
            gain.process_in_place(StereoMut::new(&mut l[..0], &mut r[..0]), &[])
                .is_err()
        );
        assert!(
            gain.process_in_place(StereoMut::new(&mut l[..64], &mut r), &[])
                .is_err()
        );
        l.fill(1.0);
        r.fill(1.0);
        gain.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
        gain.service_main_thread()?;
        assert_eq!(l, [scale * 0.25; 128]);
        assert!(gain.activate().is_err());

        let mut separate = ready(path, "separate")?;
        assert!(!separate.metadata().supports_in_place);
        assert!(
            separate
                .process_in_place(StereoMut::new(&mut l, &mut r), &[])
                .is_err()
        );
        let input = [1.0; 128];
        separate.process(Some((&input, &input)), StereoMut::new(&mut l, &mut r), &[])?;
        separate.service_main_thread()?;
        assert_eq!(l, [scale; 128]);
        assert_eq!(input, [1.0; 128]);
        let mut source = ready(path, "source")?;
        assert!(!source.metadata().has_input);
        source.process(None, StereoMut::new(&mut l, &mut r), &[])?;
        source.service_main_thread()?;
        assert_eq!(l, [scale; 128]);

        for kind in ["init-fail", "mono", "missing"] {
            assert!(open(path, kind).is_err(), "{kind}");
        }
        for kind in ["activate-fail", "start-fail"] {
            let mut failed = open(path, kind)?;
            assert!(failed.activate().is_err(), "{kind}");
        }
        for kind in ["process-fail", "nan"] {
            let mut failed = ready(path, kind)?;
            l.fill(1.0);
            r.fill(1.0);
            assert!(
                failed
                    .process_in_place(StereoMut::new(&mut l, &mut r), &[])
                    .is_err()
            );
            failed.service_main_thread()?;
            assert_eq!(l, [0.0; 128]);
            assert_eq!(r, [0.0; 128]);
        }
        let mut restarting = ready(path, "restart")?;
        restarting.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
        assert!(restarting.service_main_thread().is_err());
        // Estado inativo rejeita DSP e drop desfaz somente init.
        let mut inactive = open(path, "gain")?;
        assert!(
            inactive
                .process_in_place(StereoMut::new(&mut l, &mut r), &[])
                .is_err()
        );
        let metadata = serde_json::to_value(gain.metadata())?;
        let mut threaded = open(path, "gain")?;
        let owner = std::thread::current().id();
        let (frames, callbacks) = threaded.with_audio_worker(move |audio| {
            assert_ne!(std::thread::current().id(), owner);
            let mut left = [1.0; 480];
            let mut right = [-1.0; 480];
            let mut total = 0;
            for i in 0..2000 {
                let frames = [64, 128, 480][i % 3];
                left.fill(1.0);
                right.fill(-1.0);
                audio.process_in_place(
                    StereoMut::new(&mut left[..frames], &mut right[..frames]),
                    &[],
                )?;
                assert_eq!(left[0], scale);
                assert_eq!(right[frames - 1], -scale);
                total += frames;
            }
            Ok(total)
        })?;
        assert!(frames > 0 && callbacks > 0);
        // Owner continua utilizável após erro de trabalho; recursos voltam depois do join/stop.
        assert!(
            threaded
                .with_audio_worker::<()>(|_| Err(Error::Contract("erro deliberado do trabalho")))
                .is_err()
        );
        threaded.with_audio_worker(|_| Ok(()))?;
        let control_failure = threaded.with_audio_worker_control(
            |audio| {
                let deadline = Instant::now();
                while !audio.cancelled() && deadline.elapsed().as_secs() < 2 {
                    std::thread::yield_now();
                }
                assert!(audio.cancelled(), "Owner deve sinalizar cancelamento");
                Ok(())
            },
            || Err(Error::Contract("falha deliberada do controle")),
        );
        assert!(matches!(
            control_failure,
            Err(Error::Contract("falha deliberada do controle"))
        ));
        threaded.with_audio_worker(|_| Ok(()))?;
        let mut start_failure = open(path, "start-fail")?;
        assert!(start_failure.with_audio_worker(|_| Ok(())).is_err());
        let mut restart_worker = open(path, "restart")?;
        assert!(
            restart_worker
                .with_audio_worker(|audio| {
                    let mut l = [1.0; 64];
                    let mut r = [1.0; 64];
                    audio.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
                    Ok(())
                })
                .is_err()
        );
        // Muitos ciclos exercitam destroy/deinit/FreeLibrary (asserts nativos checam a ordem).
        for _ in 0..100 {
            drop(ready(path, "gain")?);
        }
        Ok(
            json!({"status":"PASS", "metadata":metadata, "scale":scale, "worker_frames":frames, "worker_main_callbacks":callbacks,"live_effect":live_effect,
            "checks":["sample_offset", "independent_instances", "invalid_batch_atomic", "in_place", "separate_buffers", "source", "partial_cleanup", "process_failure_silence", "nan_silence", "restart_visible", "100_lifecycle_cycles", "thread_roles"]}),
        )
    }
    fn measure(path: &Path, scale: f32) -> Result<serde_json::Value> {
        let mut results = Vec::new();
        for frames in [64, 128, 480] {
            for count in [0_usize, 1, 8] {
                let mut chain = (0..count)
                    .map(|_| ready(path, "gain"))
                    .collect::<Result<Vec<_>>>()?;
                let mut l = vec![0.0; frames];
                let mut r = vec![0.0; frames];
                l[17] = 1.0;
                r[17] = -1.0;
                for p in &mut chain {
                    p.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
                    p.service_main_thread()?;
                }
                assert_eq!(l.iter().position(|v| *v != 0.0), Some(17));
                assert_eq!(l[17], scale.powi(count as i32));
                assert_eq!(r[17], -l[17]);
                assert_eq!(l.iter().filter(|v| **v != 0.0).count(), 1);
                let mut times = vec![0_u128; 10_000];
                for iteration in 0..11_000 {
                    l.fill(0.25);
                    r.fill(-0.25);
                    let start = Instant::now();
                    for p in &mut chain {
                        p.process_in_place(StereoMut::new(&mut l, &mut r), &[])?;
                    }
                    let ns = start.elapsed().as_nanos();
                    if iteration >= 1000 {
                        times[iteration - 1000] = ns;
                    }
                    for p in &mut chain {
                        p.service_main_thread()?;
                    }
                    std::hint::black_box(&l);
                }
                times.sort_unstable();
                results.push(
                    json!({"frames":frames,"nodes":count,"median_us":times[5000] as f64/1000.0,
                    "p99_us":times[9900] as f64/1000.0,"max_us":times[9999] as f64/1000.0,
                    "additional_impulse_frames":0}),
                );
            }
        }
        Ok(
            json!({"scope":"offline_clap_c_dll_including_host_validation_and_fixture_callbacks_not_physical_latency",
            "warmup":1000,"iterations":10000,"results":results}),
        )
    }
    pub fn main() -> Result<()> {
        let args: Vec<_> = std::env::args().collect();
        if args.len() == 4 && args[1] == "--reject" {
            let expected = match args[3].as_str() {
                "abi" => "ABI CLAP incompatível",
                "entry" => "Símbolo clap_entry ausente",
                _ => return Err("Motivo esperado desconhecido".into()),
            };
            // SAFETY: variantes negativas da mesma fixture C confiável, na thread principal.
            match unsafe { OfflinePlugin::load(Path::new(&args[2]), "org.nodivu.fixture.gain") } {
                Err(Error::Contract(message)) if message == expected => {
                    println!("{}", json!({"status":"PASS", "rejected": message}));
                    return Ok(());
                }
                _ => return Err("Plugin não foi rejeitado pelo motivo esperado".into()),
            }
        }
        if args.len() != 3 {
            return Err("Uso: clap_probe <fixture.clap confiável> <escala 0.5|0.25>".into());
        }
        let scale: f32 = args[2].parse()?;
        if scale != 0.5 && scale != 0.25 {
            return Err("Escala inválida".into());
        }
        let path = Path::new(&args[1]);
        let verification = verify(path, scale)?;
        let benchmark = measure(path, scale)?;
        println!(
            "{}",
            serde_json::to_string_pretty(
                &json!({"verification":verification,"benchmark":benchmark})
            )?
        );
        Ok(())
    }
}
