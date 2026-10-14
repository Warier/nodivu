//! Explicit harness for the two trusted Nodivu utility plugins, never arbitrary untrusted DLLs.
#[cfg(not(windows))]
fn main() {
    eprintln!("Windows required");
    std::process::exit(1);
}
#[cfg(windows)]
fn main() -> Result<(), Box<dyn std::error::Error>> {
    use nodivu_block::{ParameterId, StereoMut};
    use nodivu_clap_host::{EffectControls, LiveEffect, OfflinePlugin};
    let root = std::env::args()
        .nth(1)
        .ok_or("Supply sibling publication folder")?;
    for name in ["ducking", "talk-button"] {
        let path =
            std::path::Path::new(&root).join(format!("nodivu-{name}/dist/package/{name}.clap"));
        // SAFETY: explicitly built, trusted local examples. Owner remains on main and outlives the worker.
        let mut plugin = unsafe { OfflinePlugin::load(&path, &format!("org.nodivu.{name}")) }?;
        let controls = EffectControls::prepare(plugin.metadata())?;
        plugin.with_audio_worker(|p| {
            assert_eq!(p.metadata().has_sidechain, name == "ducking");
            let mut effect = LiveEffect::prepare(p, &controls)?;
            let key = [0.7; 480];
            let silent = [0.; 480];
            let mut l = [0.5; 480];
            let mut r = [-0.25; 480];
            if name == "ducking" {
                for n in [1, 63, 480] {
                    l.fill(0.5);
                    r.fill(-0.25);
                    effect.process_with_sidechain(
                        StereoMut::new(&mut l[..n], &mut r[..n]),
                        Some((&silent[..n], &silent[..n])),
                    )?;
                    assert_eq!(l[n - 1], 0.5);
                }
                for _ in 0..100 {
                    l.fill(0.5);
                    r.fill(-0.25);
                    effect.process_with_sidechain(
                        StereoMut::new(&mut l, &mut r),
                        Some((&key, &key)),
                    )?;
                }
                assert!((l[479] / 0.5 - 10.0f32.powf(-18. / 20.)).abs() < 0.0001);
                controls.set_bypass(true);
                l.fill(0.5);
                r.fill(-0.25);
                effect
                    .process_with_sidechain(StereoMut::new(&mut l, &mut r), Some((&key, &key)))?;
                assert_eq!(l[479], 0.5);
                assert_eq!(r[479], -0.25);
                controls.set_bypass(false);
                effect.reset()?;
                l.fill(0.5);
                r.fill(-0.25);
                effect.process_with_sidechain(
                    StereoMut::new(&mut l, &mut r),
                    Some((&silent, &silent)),
                )?;
                assert_eq!(l[479], 0.5);
            } else {
                effect.process(StereoMut::new(&mut l, &mut r))?;
                assert_eq!(l, [0.; 480]);
                controls.set_parameter(ParameterId(2), 1.)?;
                l.fill(0.5);
                r.fill(-0.25);
                effect.process(StereoMut::new(&mut l, &mut r))?;
                assert_eq!(l[479], 0.5);
                controls.set_parameter(ParameterId(2), 0.)?;
                l.fill(0.5);
                r.fill(-0.25);
                effect.process(StereoMut::new(&mut l, &mut r))?;
                assert_eq!(l[479], 0.);
            }
            Ok(())
        })?;
    }
    println!(
        "PASS: actual CLAP DLLs, two independent buses, variable quantums, reduction, dry bypass, reset and talk control"
    );
    Ok(())
}
