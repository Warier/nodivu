//! Package failures are reported independently, before publishing the live backend.
use crate::{plugin, python_worker};
use nodivu_clap_host::{EffectControls, OfflinePlugin};
use nodivu_core::graph::MAX_PLUGIN_NODES;
use std::{collections::BTreeSet, path::Path};

pub struct Loaded {
    pub packages: Vec<plugin::Package>,
    pub owners: Vec<OfflinePlugin>,
    pub workers: Vec<python_worker::Package>,
}

pub fn enabled(manifest: &Path) -> bool {
    manifest.parent().is_some_and(|parent| {
        [".nodivu-disabled", ".nodivu-removed"].iter().all(|name| {
            matches!(parent.join(name).symlink_metadata(), Err(e) if e.kind() == std::io::ErrorKind::NotFound)
        })
    })
}

pub fn load(root: &Path, scanning: bool) -> Result<Loaded, Box<dyn std::error::Error>> {
    let mut paths = Vec::new();
    if root.is_file() && enabled(root) {
        paths.push(root.to_owned());
    } else if !scanning && !root.is_file() {
        return Err("Manifesto explícito ausente".into());
    }
    if scanning {
        let mut children = Vec::new();
        for entry in std::fs::read_dir(root.parent().ok_or("Pasta ausente")?)? {
            let entry = entry?;
            if !entry.file_type()?.is_dir() {
                continue;
            }
            for name in ["plugin.json", "worker.json"] {
                let file = entry.path().join(name);
                if file.is_file() && enabled(&file) {
                    children.push(file);
                }
            }
        }
        children.sort();
        paths.extend(children);
    }
    let mut loaded = Loaded {
        packages: Vec::new(),
        owners: Vec::new(),
        workers: Vec::new(),
    };
    let mut ids = BTreeSet::new();
    let mut issues = Vec::new();
    for path in paths {
        let result = (|| -> Result<(), Box<dyn std::error::Error>> {
            if loaded.packages.len() + loaded.workers.len() >= 8 {
                return Err("Limite de oito tipos ativos atingido".into());
            }
            if path.file_name().is_some_and(|n| n == "worker.json") {
                let package = python_worker::Package::read(&path)?;
                if !ids.insert(package.plugin_id.clone()) {
                    return Err("ID de plugin duplicado".into());
                }
                loaded.workers.push(package);
            } else {
                let package = plugin::Package::read(&path)?;
                if ids.contains(&package.appearance.plugin_id) {
                    return Err("ID de plugin duplicado".into());
                }
                // SAFETY: local module authorized by the user; owner remains on this thread.
                // Temporary owners drop before proceeding if preparation fails.
                let mut owner = unsafe {
                    OfflinePlugin::load(&package.library, &package.appearance.plugin_id)
                }?;
                package.validate_metadata(owner.metadata())?;
                // Activation is checked per package before entering the long-lived audio group.
                // No devices or PCM are opened here, and the scoped worker is joined.
                owner.with_audio_worker(|processor| {
                    let controls = EffectControls::prepare(processor.metadata())?;
                    nodivu_clap_host::LiveEffect::prepare(processor, &controls)?;
                    Ok(())
                })?;
                let mut group = Vec::with_capacity(MAX_PLUGIN_NODES);
                for _ in 1..MAX_PLUGIN_NODES {
                    group.push(owner.new_instance()?);
                }
                group.push(owner);
                ids.insert(package.appearance.plugin_id.clone());
                loaded.owners.extend(group);
                loaded.packages.push(package);
            }
            Ok(())
        })();
        if let Err(error) = result {
            let message: String = error.to_string().chars().take(1024).collect();
            eprintln!("Plugin indisponível ({}): {message}", path.display());
            issues.push(serde_json::json!({"manifest":path,"message":message}));
        }
    }
    // Control-plane diagnostic only. Failure to write it never invalidates working audio.
    if let Some(file) = std::env::var_os("NODIVU_PLUGIN_REPORT") {
        let write = serde_json::to_vec(&issues)
            .map_err(std::io::Error::other)
            .and_then(|bytes| std::fs::write(file, bytes));
        if let Err(error) = write {
            eprintln!("Falha ao registrar diagnóstico de plugins: {error}");
        }
    }
    Ok(loaded)
}
