//! Host privado do Electron: nenhuma interface de shell ou subcomandos.

use nodivu_core::{ApiError, Request, Response};

use nodivu_engine::{NativeBackend, app::AppBackend};

use std::process::ExitCode;

#[cfg(windows)]
mod plugin;
#[cfg(windows)]
mod plugin_loader;
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
        let plugin_loader::Loaded {
            packages,
            mut owners,
            workers,
        } = plugin_loader::load(path, scanning)?;
        if owners.is_empty() {
            let (backend, receive, controls) =
                plugin::PluginBackend::prepare(&packages, &owners, &workers)?;
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
        let (startup_send, startup_receive) =
            std::sync::mpsc::sync_channel::<(_, std::sync::Arc<plugin::PluginControls>)>(1);
        let (_, result) = nodivu_clap_host::OfflinePlugin::with_audio_group(
            &mut owners,
            move |processor| {
                // Startup handshake only, before audio/device processing. A failed prepare drops
                // the sender, so the scoped worker can exit rather than hang during shutdown.
                let Ok((receive, controls)) = startup_receive.recv() else {
                    return Ok(());
                };
                plugin::worker(processor, receive, &controls)
            },
            move |owners| -> Result<(), Box<dyn std::error::Error>> {
                // CLAP latency is authoritative only after activate, including every clone.
                let (backend, receive, controls) =
                    plugin::PluginBackend::prepare(&packages, owners, &workers)?;
                startup_send
                    .send((receive, controls.clone()))
                    .map_err(|_| "Worker indisponível na inicialização")?;
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
                })?;
                Ok(())
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
