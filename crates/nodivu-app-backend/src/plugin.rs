//! Pool CLAP limitado com estado por UUID; catálogo visual declarativo e PCM apenas no worker nativo.
#[path = "capture.rs"]
mod capture;
use nodivu_clap_host::{EffectControls, Error, LiveEffect, OfflinePlugin};
use nodivu_core::{
    DeviceInfo,
    graph::{Block, Graph, MAX_PLUGIN_NODES, NodeId},
};
use nodivu_engine::{
    AudioConfig, AudioSession, BackendError, DeviceBackend, NativeBackend,
    external_audio::ExternalAudioRun,
};
use serde::{Deserialize, Serialize};
use serde_json::{Value, json};
use std::{
    collections::BTreeMap,
    io::Read,
    path::{Path, PathBuf},
    sync::{Arc, mpsc},
    time::Duration,
};

#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Appearance {
    pub schema_version: u32,
    pub library: String,
    pub plugin_id: String,
    pub title: String,
    pub description: String,
    pub accent: String,
    pub layout: String,
    pub controls: Vec<Control>,
}
#[derive(Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct Control {
    pub parameter: u32,
    pub label: String,
    pub widget: String,
    pub unit: String,
    pub group: String,
}
#[derive(Serialize)]
struct Parameter {
    id: u32,
    min: f64,
    max: f64,
    default: f64,
    readonly: bool,
    stepped: bool,
}
pub struct Package {
    pub appearance: Appearance,
    pub library: PathBuf,
}
impl Package {
    pub fn read(path: &Path) -> Result<Self, Box<dyn std::error::Error>> {
        let mut bytes = Vec::new();
        std::fs::File::open(path)?
            .take(65537)
            .read_to_end(&mut bytes)?;
        if bytes.len() > 65536 {
            return Err("Manifesto excede 64 KiB".into());
        }
        let a: Appearance = serde_json::from_value(nodivu_core::json::parse(&bytes)?)?;
        let bounded = |s: &str, max: usize| !s.is_empty() && s.chars().count() <= max;
        if a.schema_version != 1
            || !bounded(&a.title, 64)
            || !bounded(&a.description, 240)
            || !bounded(&a.plugin_id, 128)
            || !["stack", "grid"].contains(&a.layout.as_str())
            || a.accent.len() != 7
            || !a.accent.starts_with('#')
            || !a.accent[1..].bytes().all(|v| v.is_ascii_hexdigit())
            || a.controls.len() > 32
        {
            return Err("Manifesto visual inválido".into());
        }
        for (i, c) in a.controls.iter().enumerate() {
            if !bounded(&c.label, 64)
                || c.unit.len() > 16
                || c.group.len() > 64
                || !["slider", "number", "toggle"].contains(&c.widget.as_str())
                || a.controls[..i].iter().any(|x| x.parameter == c.parameter)
            {
                return Err("Controle visual inválido".into());
            }
        }
        let parent = path
            .canonicalize()?
            .parent()
            .ok_or("Pasta de plugin ausente")?
            .to_owned();
        let library = parent.join(&a.library).canonicalize()?;
        if !library.starts_with(&parent) || library.extension().is_none_or(|s| s != "clap") {
            return Err("Biblioteca deve ser .clap dentro da pasta do plugin".into());
        }
        Ok(Self {
            appearance: a,
            library,
        })
    }
}

pub struct PluginBackend {
    controls: Arc<PluginControls>,
    slots: Vec<Option<NodeId>>,
    packages: Vec<(String, Vec<Parameter>, bool, bool)>,
    files: Vec<String>,
    capture_selections: Vec<Option<nodivu_core::capture::CaptureSelection>>,
    catalog: Value,
    send: mpsc::SyncSender<ExternalAudioRun>,
}
impl PluginBackend {
    pub fn prepare(
        packages: &[Package],
        owners: &[OfflinePlugin],
        workers: &[crate::python_worker::Package],
    ) -> Result<(Self, mpsc::Receiver<ExternalAudioRun>, Arc<PluginControls>), Error> {
        let mut catalog = Vec::new();
        let mut specs = Vec::new();
        for (i, package) in packages.iter().enumerate() {
            let metadata = owners[i * MAX_PLUGIN_NODES].metadata();
            EffectControls::prepare(metadata)?;
            for c in &package.appearance.controls {
                let p = metadata
                    .parameters
                    .iter()
                    .find(|p| p.id == c.parameter)
                    .ok_or(Error::Contract("Parâmetro visual ausente"))?;
                if c.widget == "toggle" && (p.min != 0.0 || p.max != 1.0 || !p.stepped) {
                    return Err(Error::Contract("Toggle exige inteiro 0/1"));
                }
            }
            let parameters: Vec<_> = metadata
                .parameters
                .iter()
                .map(|p| Parameter {
                    id: p.id,
                    min: p.min,
                    max: p.max,
                    default: p.default,
                    readonly: p.readonly,
                    stepped: p.stepped,
                })
                .collect();
            let mut appearance = serde_json::to_value(&package.appearance)
                .map_err(|_| Error::Contract("Manifesto inválido"))?;
            if let Some(a) = appearance.as_object_mut() {
                a.remove("library");
            }
            let file_player = owners[i * MAX_PLUGIN_NODES]
                .file_command("status", None)
                .is_ok();
            let file_transport = owners[i * MAX_PLUGIN_NODES]
                .file_transport("status", None)
                .is_ok();
            catalog.push(json!({"capture_source":owners[i * MAX_PLUGIN_NODES].capture_snapshot().is_ok(),"file_transport":file_transport,"id":metadata.id,"name":metadata.name,"appearance":appearance,"parameters":parameters,"source":!metadata.has_input,"consumer":!metadata.has_output,"inputs":if metadata.has_input {vec![0]} else {vec![]},"outputs":if metadata.has_output {vec![0]} else {vec![]},"file_player":file_player}));
            specs.push((
                metadata.id.clone(),
                parameters,
                !metadata.has_input,
                !metadata.has_output,
            ));
        }
        for package in workers {
            catalog.push(package.catalog());
            specs.push((
                package.plugin_id.clone(),
                vec![Parameter {
                    id: 7,
                    min: 0.0,
                    max: 4.0,
                    default: 1.0,
                    readonly: false,
                    stepped: false,
                }],
                false,
                false,
            ));
        }
        let count = owners.len() + workers.len() * MAX_PLUGIN_NODES;
        let (send, receive) = mpsc::sync_channel(1);
        let controls = Arc::new(PluginControls {
            capture: capture::CaptureControls::prepare(owners)?,
            slots: (0..count).map(|_| InstanceMailbox::default()).collect(),
            status: (0..count)
                .map(|_| std::sync::atomic::AtomicI32::new(0))
                .collect(),
            position_ms: (0..count)
                .map(|_| std::sync::atomic::AtomicU32::new(0))
                .collect(),
            duration_ms: (0..count)
                .map(|_| std::sync::atomic::AtomicU32::new(0))
                .collect(),
            commands: std::sync::Mutex::new(Vec::new()),
            native_count: owners.len(),
            workers: workers.to_vec(),
            worker_states: (0..workers.len() * MAX_PLUGIN_NODES)
                .map(|_| Arc::new(crate::python_worker::State::default()))
                .collect(),
        });
        Ok((
            Self {
                controls: controls.clone(),
                slots: vec![None; count],
                files: vec![String::new(); count],
                capture_selections: vec![None; count],
                packages: specs,
                catalog: json!(catalog),
                send,
            },
            receive,
            controls,
        ))
    }
}
impl DeviceBackend for PluginBackend {
    fn capture_targets(&self, refresh: bool) -> Result<Value, BackendError> {
        self.capture_targets_impl(refresh)
    }
    fn capture_configure(
        &mut self,
        id: NodeId,
        selection: Option<nodivu_core::capture::CaptureSelection>,
        token: Option<&str>,
    ) -> Result<(), BackendError> {
        self.capture_configure_impl(id, selection, token)
    }

    fn plugin_latency(&self, id: &str) -> u32 {
        self.catalog
            .as_array()
            .into_iter()
            .flatten()
            .find(|p| p["id"].as_str() == Some(id))
            .and_then(|p| p["latency_frames"].as_u64())
            .unwrap_or(0) as u32
    }
    fn enumerate(&mut self) -> Result<Vec<DeviceInfo>, BackendError> {
        NativeBackend.enumerate()
    }
    fn device_inventory(
        &mut self,
    ) -> Result<nodivu_engine::discovery::DeviceInventory, BackendError> {
        NativeBackend.device_inventory()
    }
    fn new_id(&mut self) -> Result<uuid::Uuid, BackendError> {
        NativeBackend.new_id()
    }
    fn plugin_catalog(&self) -> Value {
        self.catalog.clone()
    }
    fn plugin_block(&self, id: &str) -> Result<Block, BackendError> {
        let (id, parameters, source, consumer) = self
            .packages
            .iter()
            .find(|p| p.0 == id)
            .ok_or_else(|| invalid("Plugin não encontrado"))?;
        Ok(Block::Plugin {
            plugin_id: id.clone(),
            source: *source,
            consumer: *consumer,
            parameters: parameters
                .iter()
                .filter(|p| !p.readonly)
                .map(|p| (p.id, p.default))
                .collect::<BTreeMap<_, _>>(),
            bypass: false,
        })
    }
    fn validate_plugins(&self, graph: &Graph) -> Result<(), BackendError> {
        for node in &graph.nodes {
            if let Block::Plugin {
                plugin_id,
                parameters,
                source,
                consumer,
                ..
            } = &node.block
            {
                let (_, specs, expected_source, expected_consumer) = self
                    .packages
                    .iter()
                    .find(|p| &p.0 == plugin_id)
                    .ok_or_else(|| invalid("Plugin não encontrado"))?;
                if source != expected_source
                    || consumer != expected_consumer
                    || parameters.len() != specs.iter().filter(|p| !p.readonly).count()
                {
                    return Err(invalid("Identidade ou conjunto de parâmetros inválido"));
                }
                if self
                    .slots
                    .iter()
                    .position(|id| *id == Some(node.id))
                    .is_some_and(|i| self.packages[i / MAX_PLUGIN_NODES].0 != *plugin_id)
                {
                    return Err(invalid("Tipo de plugin de um UUID é imutável"));
                }
                // Validar o lote inteiro antes de publicar qualquer valor.
                for (id, value) in parameters {
                    if !specs.iter().any(|p| {
                        p.id == *id
                            && !p.readonly
                            && value.is_finite()
                            && (p.min..=p.max).contains(value)
                            && (!p.stepped || value.fract() == 0.0)
                    }) {
                        return Err(invalid("Valor de parâmetro inválido"));
                    }
                }
            }
        }
        Ok(())
    }
    fn configure_plugins(&mut self, graph: &Graph) -> Result<(), BackendError> {
        self.validate_plugins(graph)?;
        // Preserve slot identity through document reordering. Reuse only resets on audio thread;
        // publication carries UUID, so an old plan never processes a different node's settings.
        for (index, slot) in self.slots.iter_mut().enumerate() {
            if slot.is_some_and(|id| {
                !graph
                    .nodes
                    .iter()
                    .any(|n| n.id == id && n.block.kind() == "plugin")
            }) {
                self.controls.capture.clear(index)?;
                self.capture_selections[index] = None;
                *slot = None;
            }
        }
        for node in &graph.nodes {
            if let Block::Plugin {
                plugin_id,
                parameters,
                bypass,
                ..
            } = &node.block
            {
                let package_index = self
                    .packages
                    .iter()
                    .position(|p| &p.0 == plugin_id)
                    .ok_or_else(|| invalid("Plugin não encontrado"))?;
                let start = package_index * MAX_PLUGIN_NODES;
                let index = self
                    .slots
                    .iter()
                    .position(|id| *id == Some(node.id))
                    .or_else(|| {
                        self.slots[start..start + MAX_PLUGIN_NODES]
                            .iter()
                            .position(Option::is_none)
                            .map(|i| start + i)
                    })
                    .ok_or_else(|| invalid("Pool de plugins cheio"))?;
                if self.slots[index] != Some(node.id) {
                    self.files[index].clear();
                    self.controls.capture.clear(index)?;
                    self.capture_selections[index] = None;
                    self.controls.status[index].store(0, SeqCst);
                    let mut commands = self
                        .controls
                        .commands
                        .lock()
                        .map_err(|_| invalid("Controle indisponível"))?;
                    // Reuse invalidates queued actions for the old occupant. One clear per
                    // slot, plus at most 32 user actions: bounded even before maintenance.
                    commands.retain(|command| command.0 != index);
                    commands.push((index, "clear".into(), None, None));
                    self.controls.position_ms[index].store(0, SeqCst);
                    self.controls.duration_ms[index].store(0, SeqCst);
                }
                self.slots[index] = Some(node.id);
                let mut state = InstanceState {
                    id: Some(node.id),
                    bypass: *bypass,
                    ..InstanceState::default()
                };
                for (i, p) in self.packages[package_index].1.iter().enumerate() {
                    state.values[i] = parameters.get(&p.id).copied().unwrap_or(p.default);
                }
                self.controls.slots[index].publish(state);
            }
        }
        for (id, slot) in self.slots.iter().zip(&self.controls.slots) {
            if id.is_none() {
                slot.publish(InstanceState::default());
            }
        }
        for (i, state) in self.controls.worker_states.iter().enumerate() {
            state
                .active
                .store(self.slots[self.controls.native_count + i].is_some(), SeqCst);
        }
        Ok(())
    }
    fn plugin_runtime(&self) -> Value {
        let mut result = serde_json::Map::new();
        for (i, id) in self.slots.iter().enumerate() {
            if let Some(id) = id {
                result.insert(
                    id.0.to_string(),
                    if i >= self.controls.native_count {
                        self.controls.worker_states[i - self.controls.native_count].snapshot()
                    } else {
                        json!({"capture":self.controls.capture.state(i),"capture_selection":self.capture_selections[i],"status":self.controls.status[i].load(SeqCst),"file":self.files[i],"position_ms":self.controls.position_ms[i].load(SeqCst),"duration_ms":self.controls.duration_ms[i].load(SeqCst)})
                    },
                );
            }
        }
        Value::Object(result)
    }
    fn plugin_command(
        &mut self,
        id: NodeId,
        action: &str,
        path: Option<&str>,
        position_ms: Option<u32>,
    ) -> Result<(), BackendError> {
        let index = self
            .slots
            .iter()
            .position(|n| *n == Some(id))
            .ok_or_else(|| invalid("Nó ausente"))?;
        if index >= self.controls.native_count {
            if action != "restart" || path.is_some() || position_ms.is_some() {
                return Err(invalid("Worker aceita somente reiniciar sem caminho"));
            }
            self.controls.worker_states[index - self.controls.native_count]
                .restart
                .fetch_add(1, SeqCst);
            return Ok(());
        }
        if !self.catalog[index / MAX_PLUGIN_NODES]["file_player"]
            .as_bool()
            .unwrap_or(false)
        {
            return Err(invalid("Plugin sem controle de arquivo"));
        }
        let transport = self.catalog[index / MAX_PLUGIN_NODES]["file_transport"]
            .as_bool()
            .unwrap_or(false);
        if (action != "seek" && position_ms.is_some()) || (action != "load" && path.is_some()) {
            return Err(invalid("Argumento incompatível com ação"));
        }
        let path = match action {
            "load" => {
                let p = Path::new(path.ok_or_else(|| invalid("Caminho ausente"))?);
                if !p.is_absolute()
                    || !p.is_file()
                    || p.extension().is_none_or(|s| {
                        !s.eq_ignore_ascii_case("mp3")
                            && !(transport
                                && (s.eq_ignore_ascii_case("wav") || s.eq_ignore_ascii_case("m4a")))
                    })
                    || p.metadata()
                        .map_err(|e| BackendError::new("arquivo", e))?
                        .len()
                        > (if transport { 256 } else { 32 }) * 1024 * 1024
                {
                    return Err(invalid(if transport {
                        "Escolha MP3, WAV ou M4A local de até 256 MiB"
                    } else {
                        "Escolha MP3 local de até 32 MiB"
                    }));
                }
                Some(
                    p.to_str()
                        .ok_or_else(|| invalid("Caminho não UTF-8"))?
                        .to_owned(),
                )
            }
            "play" | "pause" | "seek"
                if (2..=if transport { 5 } else { 4 })
                    .contains(&self.controls.status[index].load(SeqCst))
                    && (action == "play" || transport)
                    && (action != "seek"
                        || position_ms.is_some_and(|p| {
                            p <= self.controls.duration_ms[index].load(SeqCst)
                        })) =>
            {
                None
            }
            _ => return Err(invalid("Ação inválida ou arquivo ainda não pronto")),
        };
        let mut queue = self
            .controls
            .commands
            .lock()
            .map_err(|_| invalid("Controle indisponível"))?;
        if queue.len() >= 32 {
            return Err(invalid("Controle ocupado; tente novamente"));
        }
        if let Some(path) = &path {
            self.files[index] = path.clone();
            self.controls.status[index].store(1, SeqCst);
            self.controls.position_ms[index].store(0, SeqCst);
            self.controls.duration_ms[index].store(0, SeqCst);
        }
        queue.push((index, action.to_owned(), path, position_ms));
        Ok(())
    }
    fn start_audio(&mut self, config: AudioConfig) -> Result<AudioSession, BackendError> {
        let (run, session) = ExternalAudioRun::prepare_session(config)?;
        self.send.try_send(run).map_err(|e| {
            BackendError::new(
                "preparar áudio",
                std::io::Error::other(format!(
                    "Worker ocupado/desconectado; use Reabrir dispositivos: {e}"
                )),
            )
        })?;
        Ok(session)
    }
}
fn invalid(message: &'static str) -> BackendError {
    BackendError::new(
        "validar plugin",
        std::io::Error::new(std::io::ErrorKind::InvalidInput, message),
    )
}

use std::sync::atomic::{AtomicU64, Ordering::SeqCst};

#[derive(Clone, Copy, Default)]
struct InstanceState {
    id: Option<NodeId>,
    values: [f64; 32],
    bypass: bool,
}
#[derive(Default)]
struct InstanceMailbox {
    sequence: AtomicU64,
    high: AtomicU64,
    low: AtomicU64,
    flags: AtomicU64,
    values: [AtomicU64; 32],
}
impl InstanceMailbox {
    fn publish(&self, state: InstanceState) {
        self.sequence.fetch_add(1, SeqCst);
        let id = state.id.map_or(0, |id| id.0.as_u128());
        self.high.store((id >> 64) as u64, SeqCst);
        self.low.store(id as u64, SeqCst);
        self.flags.store(
            u64::from(state.id.is_some()) | (u64::from(state.bypass) << 1),
            SeqCst,
        );
        for (target, value) in self.values.iter().zip(state.values) {
            target.store(value.to_bits(), SeqCst);
        }
        self.sequence.fetch_add(1, SeqCst);
    }
    fn read_for(&self, requested: NodeId) -> Option<InstanceState> {
        let before = self.sequence.load(SeqCst);
        if before & 1 != 0 {
            return None;
        }
        let id = NodeId(uuid::Uuid::from_u128(
            (u128::from(self.high.load(SeqCst)) << 64) | u128::from(self.low.load(SeqCst)),
        ));
        let flags = self.flags.load(SeqCst);
        if flags & 1 == 0 || id != requested {
            return None;
        }
        let state = InstanceState {
            id: Some(id),
            bypass: flags & 2 != 0,
            values: std::array::from_fn(|i| f64::from_bits(self.values[i].load(SeqCst))),
        };
        (self.sequence.load(SeqCst) == before).then_some(state)
    }
}
type FileCommand = (usize, String, Option<String>, Option<u32>);
pub struct PluginControls {
    capture: capture::CaptureControls,
    native_count: usize,
    workers: Vec<crate::python_worker::Package>,
    worker_states: Vec<Arc<crate::python_worker::State>>,
    slots: Vec<InstanceMailbox>,
    status: Vec<std::sync::atomic::AtomicI32>,
    position_ms: Vec<std::sync::atomic::AtomicU32>,
    duration_ms: Vec<std::sync::atomic::AtomicU32>,
    // Accessed only by command/main maintenance, NEVER audio worker.
    commands: std::sync::Mutex<Vec<FileCommand>>,
}
impl PluginControls {
    pub fn maintain(&self, owners: &[OfflinePlugin]) {
        self.capture.maintain(owners);
        if let Ok(mut queue) = self.commands.lock() {
            for (i, action, path, position_ms) in queue.drain(..) {
                if i >= owners.len() {
                    continue;
                }
                let result = if ["play", "pause", "seek"].contains(&action.as_str())
                    && owners[i].file_transport("status", None).is_ok()
                {
                    owners[i]
                        .file_transport(&action, position_ms)
                        .map(|(_, _, status)| status)
                } else {
                    owners[i].file_command(&action, path.as_deref())
                };
                if let Ok(status) = result {
                    self.status[i].store(status, SeqCst);
                } else if action != "clear" {
                    self.status[i].store(-1, SeqCst);
                }
            }
        }
        for (i, owner) in owners.iter().enumerate() {
            if let Ok((position, duration, status)) = owner.file_transport("status", None) {
                self.status[i].store(status, SeqCst);
                self.position_ms[i].store(position, SeqCst);
                self.duration_ms[i].store(duration, SeqCst);
            } else if let Ok(status) = owner.file_command("status", None) {
                self.status[i].store(status, SeqCst);
            }
        }
    }
}

pub fn worker(
    processors: &mut [nodivu_clap_host::AudioProcessor<'_>],
    receive: mpsc::Receiver<ExternalAudioRun>,
    mailbox: &PluginControls,
) -> Result<(), Error> {
    // All allocation occurs before WASAPI processing. DLL instances live until scoped join.
    let controls = processors
        .iter()
        .map(|p| EffectControls::prepare(p.metadata()))
        .collect::<Result<Vec<_>, _>>()?;
    let parameters: Vec<Vec<_>> = processors
        .iter()
        .map(|p| {
            p.metadata()
                .parameters
                .iter()
                .enumerate()
                .filter(|(_, p)| !p.readonly)
                .map(|(i, p)| (i, nodivu_block::ParameterId(p.id)))
                .collect()
        })
        .collect();
    let mut effects = processors
        .iter_mut()
        .zip(&controls)
        .map(|(p, c)| LiveEffect::prepare(p, c))
        .collect::<Result<Vec<_>, _>>()?;
    let mut workers = mailbox
        .workers
        .iter()
        .flat_map(|package| (0..MAX_PLUGIN_NODES).map(move |_| package.clone()))
        .zip(&mailbox.worker_states)
        .map(|(package, state)| crate::python_worker::Instance::prepare(package, Arc::clone(state)))
        .collect::<Result<Vec<_>, _>>()
        .map_err(|_| Error::Contract("Não foi possível preparar serviço de worker"))?;
    let mut cached = vec![InstanceState::default(); mailbox.slots.len()];
    while !effects.first().is_some_and(|effect| effect.cancelled()) {
        let run = match receive.recv_timeout(Duration::from_millis(10)) {
            Ok(run) => run,
            Err(mpsc::RecvTimeoutError::Timeout) => continue,
            Err(mpsc::RecvTimeoutError::Disconnected) => break,
        };
        let stop = run.stop_callback();
        // A new hardware session must never play PCM retained from the old endpoint.
        for worker in &mut workers {
            worker
                .reset()
                .map_err(|_| Error::Contract("Reset da sessão worker falhou"))?;
        }
        for (index, effect) in effects.iter_mut().enumerate() {
            if mailbox.capture.supports(index) {
                effect.reset()?;
            }
        }
        let mut failure = None;
        let _audio_result = run.run(&mut |audio, id| {
            if effects.first().is_some_and(|effect| effect.cancelled()) {
                stop();
                return false;
            }
            if failure.is_some() {
                return false;
            }
            let Some(id) = id else {
                return true;
            };
            let found = mailbox
                .slots
                .iter()
                .enumerate()
                .find_map(|(i, slot)| slot.read_for(id).map(|state| (i, state)));
            // A concurrent edit never blocks audio. Use prior coherent settings only for the same UUID.
            let found = found.or_else(|| {
                cached
                    .iter()
                    .enumerate()
                    .find(|(_, s)| s.id == Some(id))
                    .map(|(i, s)| (i, *s))
            });
            let Some((index, state)) = found else {
                audio.left.fill(0.0);
                audio.right.fill(0.0);
                return true;
            };
            let result = (|| {
                if index >= effects.len() {
                    let worker = &mut workers[index - effects.len()];
                    if cached[index].id != Some(id) {
                        worker
                            .reset()
                            .map_err(|_| Error::Contract("Reset do worker falhou"))?;
                    }
                    cached[index] = state;
                    return worker
                        .process(audio, state.values[0] as f32, state.bypass)
                        .map_err(|_| Error::Contract("Buffer/controle do worker inválido"));
                }
                for &(i, id) in &parameters[index] {
                    controls[index].set_parameter(id, state.values[i])?;
                }
                controls[index].set_bypass(state.bypass);
                if cached[index].id != Some(id) {
                    effects[index].reset()?;
                }
                cached[index] = state;
                effects[index].process(audio).map(|_| ())
            })();
            if let Err(error) = result {
                failure = Some(error);
                stop();
                false
            } else {
                true
            }
        });
        if let Some(error) = failure {
            return Err(error);
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn concurrent_settings_never_mix_uuid_bypass_or_parameter_batch() {
        let mailbox = Arc::new(InstanceMailbox::default());
        let writer = mailbox.clone();
        let id = NodeId(uuid::Uuid::from_u128(7));
        let thread = std::thread::spawn(move || {
            for i in 1..10_000 {
                writer.publish(InstanceState {
                    id: Some(id),
                    values: [f64::from(i); 32],
                    bypass: i % 2 == 0,
                });
            }
        });
        while !thread.is_finished() {
            if let Some(state) = mailbox.read_for(id) {
                assert!(state.values.iter().all(|v| *v == state.values[0]));
                assert_eq!(state.bypass, (state.values[0] as u64).is_multiple_of(2));
                assert_eq!(state.id, Some(id));
            }
        }
        thread.join().unwrap();
        assert!(mailbox.read_for(NodeId(uuid::Uuid::from_u128(8))).is_none());
        mailbox.sequence.store(1, SeqCst);
        assert!(mailbox.read_for(id).is_none());
    }
}
