//! Host privado do Electron: nenhuma interface de shell ou subcomandos.

use nodivu_core::{ApiError, Request, Response};

use nodivu_engine::{NativeBackend, app::AppBackend};

use std::process::ExitCode;

#[cfg(windows)]
mod plugin;
#[cfg(windows)]
mod python_worker;

fn main() -> ExitCode {
    match run() {
        Ok(()) => ExitCode::SUCCESS,
        Err(error) => {
            eprintln!("{error}");
            ExitCode::FAILURE
        }
    }
}

fn run() -> Result<(), Box<dyn std::error::Error>> {
    #[cfg(windows)]
    if let Some(path) = std::env::var_os("NODIVU_PLUGIN_MANIFEST") {
        let path = std::path::Path::new(&path);
        let scanning = std::env::var_os("NODIVU_SCAN_PLUGINS").is_some();
        let mut packages = if path.is_file() {
            vec![plugin::Package::read(path)?]
        } else if scanning {
            Vec::new()
        } else {
            return Err("Manifesto explícito ausente".into());
        };
        let mut workers = Vec::new();
        // Explicit root manifest plus one-level child packages; deterministic and bounded.
        if scanning {
            let mut paths = std::fs::read_dir(path.parent().ok_or("Pasta ausente")?)?
                .collect::<Result<Vec<_>, _>>()?
                .into_iter()
                .map(|e| e.path().join("plugin.json"))
                .filter(|p| p.is_file())
                .collect::<Vec<_>>();
            paths.sort();
            if paths.len() + packages.len() > 8 {
                return Err("Até oito tipos de plugin".into());
            }
            for path in paths {
                packages.push(plugin::Package::read(&path)?);
            }
            let mut paths = std::fs::read_dir(path.parent().ok_or("Pasta ausente")?)?
                .collect::<Result<Vec<_>, _>>()?
                .into_iter()
                .map(|e| e.path().join("worker.json"))
                .filter(|p| p.is_file())
                .collect::<Vec<_>>();
            paths.sort();
            if paths.len() + packages.len() > 8 {
                return Err("Até oito tipos de plugin/worker".into());
            }
            for path in paths {
                workers.push(python_worker::Package::read(&path)?);
            }
        }
        let mut owners = Vec::new();
        let mut ids = std::collections::BTreeSet::new();
        for package in &packages {
            if !ids.insert(&package.appearance.plugin_id) {
                return Err("ID de plugin duplicado".into());
            }
            // SAFETY: trusted local module, main-thread owners, scoped join before unload.
            let owner = unsafe {
                nodivu_clap_host::OfflinePlugin::load(
                    &package.library,
                    &package.appearance.plugin_id,
                )
            }?;
            for _ in 1..nodivu_core::graph::MAX_PLUGIN_NODES {
                owners.push(owner.new_instance()?);
            }
            owners.push(owner);
        }
        for worker in &workers {
            if !ids.insert(&worker.plugin_id) {
                return Err("ID de plugin/worker duplicado".into());
            }
        }
        let (backend, receive, controls) =
            plugin::PluginBackend::prepare(&packages, &owners, &workers)?;
        if owners.is_empty() {
            // Python-only packages need no dummy CLAP module. Scope joins before controls drop.
            return std::thread::scope(|scope| -> Result<(), Box<dyn std::error::Error>> {
                let worker = std::thread::Builder::new()
                    .name("nodivu-audio".into())
                    .spawn_scoped(scope, || plugin::worker(&mut [], receive, &controls))?;
                let result = serve(backend, || {
                    if worker.is_finished() {
                        return Err(ApiError::new(
                            nodivu_core::ErrorCode::BackendError,
                            "Worker de áudio encerrou; reconecte o motor.",
                        ));
                    }
                    Ok(())
                });
                worker
                    .join()
                    .map_err(|_| "Worker de áudio interrompido")??;
                Ok(result?)
            });
        }
        let (_, result) = nodivu_clap_host::OfflinePlugin::with_audio_group(
            &mut owners,
            |processor| plugin::worker(processor, receive, &controls),
            |owners| {
                serve(backend, || {
                    controls.maintain(owners);
                    if owners.iter().any(|o| o.worker_cancelled()) {
                        return Err(ApiError::new(
                            nodivu_core::ErrorCode::BackendError,
                            "Worker do plugin encerrou; reconecte o motor.",
                        ));
                    }

                    for owner in owners {
                        owner.service_main_thread().map_err(|e| {
                            ApiError::new(nodivu_core::ErrorCode::BackendError, e.to_string())
                        })?;
                    }

                    Ok(())
                })
            },
        )?;

        result?;

        return Ok(());
    }

    Ok(serve(NativeBackend, || Ok(()))?)
}

fn serve<B: nodivu_engine::DeviceBackend>(
    backend: B,
    mut maintenance: impl FnMut() -> Result<(), ApiError>,
) -> Result<(), ApiError> {
    let mut backend = AppBackend::new(backend);

    let result = nodivu_ipc::serve_with_maintenance(
        |bytes| {
            let mut id = None;

            let result = nodivu_core::json::parse(bytes).and_then(|value| {
                id = value
                    .get("id")
                    .and_then(|v| v.as_str())
                    .filter(|s| (1..=64).contains(&s.chars().count()))
                    .map(str::to_owned);

                serde_json::from_value::<Request>(value)
                    .map_err(|e| ApiError::invalid(e.to_string()))
                    .and_then(|r| backend.request(r))
            });

            (Response::new(id, result), backend.shutting_down())
        },
        || {
            maintenance()?;

            #[cfg(windows)]
            if !nodivu_clap_host::pump_main_thread() {
                return Err(ApiError::new(
                    nodivu_core::ErrorCode::InternalError,
                    "Encerramento solicitado pelo ciclo nativo.",
                ));
            }

            Ok(())
        },
    );

    result.and(backend.stop())
}
