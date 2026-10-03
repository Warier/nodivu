//! Backend do produto: documento de blocos e vida dos dispositivos independentes dos fios.
use crate::{AudioConfig, AudioSession, DeviceBackend};
use nodivu_core::{graph::*, *};
use serde::Deserialize;
use serde_json::{Value, json};

#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Apply {
    expected_revision: u64,
    graph: Graph,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Add {
    expected_revision: u64,
    kind: String,
    #[serde(default)]
    plugin_id: Option<String>,
}
#[derive(Deserialize)]
#[serde(deny_unknown_fields)]
struct Mute {
    muted: bool,
}

pub struct AppBackend<B> {
    backend: B,
    graph: Graph,
    revision: u64,
    muted: bool,
    audio: Option<AudioSession>,
    opened: Option<(Option<DeviceId>, DeviceId)>,
    last_error: Option<ApiError>,
    generation: u64,
    shutdown: bool,
    suspended: bool,
}
impl<B: DeviceBackend> AppBackend<B> {
    pub fn new(backend: B) -> Self {
        Self {
            backend,
            graph: Graph::default(),
            revision: 0,
            muted: false,
            audio: None,
            opened: None,
            last_error: None,
            generation: 0,
            shutdown: false,
            suspended: false,
        }
    }
    pub fn shutting_down(&self) -> bool {
        self.shutdown
    }
    pub fn stop(&mut self) -> Result<(), ApiError> {
        if let Some(mut audio) = self.audio.take() {
            audio
                .stop()
                .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))?;
        }
        self.opened = None;
        Ok(())
    }
    fn revision(&self, expected: u64) -> Result<(), ApiError> {
        if expected != self.revision {
            Err(ApiError::new(
                ErrorCode::RevisionConflict,
                "Projeto mudou; atualize o snapshot.",
            ))
        } else {
            Ok(())
        }
    }
    pub fn snapshot(&self) -> Value {
        json!({"suspended":self.suspended,"plugin_states":self.backend.plugin_runtime(),"revision":self.revision,"graph":self.graph,"muted":self.muted,"engine_state":self.audio.as_ref().map_or("idle",|a|a.state()),"generation":self.generation,
            "signal":if self.muted { 0 } else { self.graph.live_plan(self.muted, |id| self.backend.plugin_latency(id)).map_or(0, |p| p.signal.source) },"metrics":self.audio.as_ref().map(|a|a.metrics()),
            "last_error":self.audio.as_ref().and_then(|a|a.error()).or_else(||self.last_error.clone()),
            "capture_error":self.audio.as_ref().and_then(|a|a.capture_error())})
    }
    fn reconcile(&mut self, retry: bool) {
        if self.suspended {
            return;
        }
        let capture = self.graph.endpoint(true);
        let output = self.graph.endpoint(false);
        let desired = output.map(|output| (capture, output));
        let plan = match self
            .graph
            .live_plan(self.muted, |id| self.backend.plugin_latency(id))
        {
            Ok(plan) => plan,
            Err(e) => {
                self.last_error = Some(e);
                return;
            }
        };
        if self.opened == desired && !retry {
            if let Some(audio) = &mut self.audio
                && let Err(error) = audio.set_render_plan(plan)
            {
                self.last_error = Some(error);
            }
            return;
        }
        if let Err(error) = self.stop() {
            self.last_error = Some(error);
            return;
        }
        self.last_error = None;
        if let Some((capture, output)) = desired {
            // Uma tentativa por seleção/retry; não migrar endpoints nem repetir abertura em polling.
            self.opened = Some((capture.clone(), output.clone()));
            match self.backend.start_audio(AudioConfig {
                capture,
                output,
                plan,
            }) {
                Ok(audio) => {
                    self.audio = Some(audio);
                    self.generation += 1;
                }
                Err(error) => {
                    self.last_error =
                        Some(ApiError::new(ErrorCode::BackendError, error.to_string()))
                }
            }
        }
    }
    fn apply(&mut self, mut graph: Graph) -> Result<Value, ApiError> {
        graph.validate()?;
        graph.live_plan(self.muted, |id| self.backend.plugin_latency(id))?;
        graph.schema_version = 2;
        for node in &graph.nodes {
            if let Ok(previous) = self.graph.node(node.id)
                && previous.block.kind() != node.block.kind()
            {
                return Err(ApiError::invalid(
                    "O tipo de um bloco existente é imutável; remova e crie outro.",
                ));
            }
        }
        for capture in [true, false] {
            let id = graph.endpoint(capture);
            if id == self.graph.endpoint(capture) {
                continue;
            }
            if let Some(id) = id {
                let devices = self
                    .backend
                    .enumerate()
                    .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))?;
                let flow = if capture { Flow::Capture } else { Flow::Render };
                if !devices.iter().any(|d| {
                    d.endpoint_id == id && d.flow == flow && d.state == DeviceState::Active
                }) {
                    return Err(ApiError::new(
                        ErrorCode::DeviceUnavailable,
                        "Selecione um dispositivo ativo da direção correta.",
                    ));
                }
            }
        }
        let next_revision = if graph != self.graph {
            self.revision
                .checked_add(1)
                .ok_or_else(|| ApiError::new(ErrorCode::ResourceLimit, "Limite de revisões."))?
        } else {
            self.revision
        };
        self.backend
            .configure_plugins(&graph)
            .map_err(|e| ApiError::invalid(e.to_string()))?;
        if graph != self.graph {
            self.revision = next_revision;
            self.graph = graph;
        }
        self.reconcile(false);
        Ok(self.snapshot())
    }
    pub fn request(&mut self, request: Request) -> Result<Value, ApiError> {
        request.validate()?;
        match request.command.as_str() {
            "capture.targets" => {
                #[derive(Deserialize)]
                #[serde(deny_unknown_fields)]
                struct Input {
                    #[serde(default)]
                    refresh: bool,
                }
                let p: Input = params(request.params)?;
                self.backend
                    .capture_targets(p.refresh)
                    .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))
            }
            "capture.configure" => {
                #[derive(Deserialize)]
                #[serde(deny_unknown_fields)]
                struct Input {
                    node_id: NodeId,
                    selection: Option<nodivu_core::capture::CaptureSelection>,
                    token: Option<String>,
                }
                let p: Input = params(request.params)?;
                self.graph.node(p.node_id)?;
                if let Some(selection) = &p.selection {
                    selection.validate()?;
                }
                self.backend
                    .capture_configure(p.node_id, p.selection, p.token.as_deref())
                    .map_err(|e| ApiError::invalid(e.to_string()))?;
                Ok(self.snapshot())
            }
            "system.hello" => {
                empty(request.params)?;
                Ok(
                    json!({"protocol_version":1,"backend":"nodivu-app-backend","api":"graph-v2","blocks":["capture","output","gain","tone","mixer","meter"],"max_gain_blocks":8,"max_plugin_blocks":8,"max_nodes":32,"max_edges":64,"live_connections":true,"accepted_graph_versions":[1,2],"ports":{"mixer":{"inputs":[0,1,2,3],"outputs":[0]},"meter":{"inputs":[0],"outputs":[]}},"fanout":true}),
                )
            }
            "plugin.command" => {
                #[derive(serde::Deserialize)]
                #[serde(deny_unknown_fields)]
                struct Params {
                    node_id: nodivu_core::graph::NodeId,
                    action: String,
                    path: Option<String>,
                    position_ms: Option<u32>,
                }
                let p: Params = serde_json::from_value(request.params)
                    .map_err(|e| ApiError::invalid(e.to_string()))?;
                self.graph.node(p.node_id)?;
                self.backend
                    .plugin_command(p.node_id, &p.action, p.path.as_deref(), p.position_ms)
                    .map_err(|e| ApiError::invalid(e.to_string()))?;
                Ok(self.snapshot())
            }
            "plugins.list" => {
                empty(request.params)?;
                Ok(json!({"plugins":self.backend.plugin_catalog(), "max_instances":8}))
            }
            "devices.list" => {
                empty(request.params)?;
                Ok(json!(self.backend.device_inventory().map_err(|e| {
                    ApiError::new(ErrorCode::BackendError, e.to_string())
                })?))
            }
            "session.snapshot" => {
                empty(request.params)?;
                Ok(self.snapshot())
            }
            "project.validate" | "project.open" => {
                #[derive(Deserialize)]
                #[serde(deny_unknown_fields)]
                struct Input {
                    contents: String,
                    expected_revision: u64,
                }
                let p: Input = params(request.params)?;
                self.revision(p.expected_revision)?;
                let project = nodivu_core::project::Project::parse(&p.contents)?;
                project
                    .graph
                    .live_plan(false, |id| self.backend.plugin_latency(id))?;
                self.backend
                    .validate_plugins(&project.graph)
                    .map_err(|e| ApiError::invalid(e.to_string()))?;
                let catalog = self.backend.plugin_catalog();
                for resource in &project.resources {
                    let Block::Plugin { plugin_id, .. } =
                        &project.graph.node(resource.node_id)?.block
                    else {
                        return Err(ApiError::invalid("Recurso sem plugin."));
                    };
                    if !catalog.as_array().is_some_and(|items| {
                        items
                            .iter()
                            .any(|item| item["id"] == *plugin_id && item["file_player"] == true)
                    }) {
                        return Err(ApiError::invalid("Plugin sem suporte a arquivo."));
                    }
                }
                for source in &project.captures {
                    let Block::Plugin { plugin_id, .. } =
                        &project.graph.node(source.node_id)?.block
                    else {
                        return Err(ApiError::invalid("Captura sem plugin."));
                    };
                    if !catalog.as_array().is_some_and(|items| {
                        items
                            .iter()
                            .any(|item| item["id"] == *plugin_id && item["capture_source"] == true)
                    }) {
                        return Err(ApiError::invalid("Plugin sem captura de aplicativos."));
                    }
                }
                if request.command == "project.validate" {
                    return Ok(json!({"project":project}));
                }
                let revision = self
                    .revision
                    .checked_add(1)
                    .ok_or_else(|| ApiError::invalid("Limite de revisões."))?;
                // Validate the whole document before stopping the old session. Resources are
                // references only: opening a project never reads/plays its audio files.
                self.stop()?;
                self.suspended = true;
                self.backend
                    .configure_plugins(&Graph::default())
                    .and_then(|()| self.backend.configure_plugins(&project.graph))
                    .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))?;
                for source in &project.captures {
                    self.backend
                        .capture_configure(source.node_id, Some(source.selection.clone()), None)
                        .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))?;
                }
                self.graph = project.graph.clone();
                self.revision = revision;
                self.last_error = None;
                self.muted = false;
                Ok(json!({"snapshot":self.snapshot(),"project":project}))
            }
            "graph.apply" => {
                let p: Apply = params(request.params)?;
                self.revision(p.expected_revision)?;
                self.apply(p.graph)
            }
            "node.add" => {
                let p: Add = params(request.params)?;
                self.revision(p.expected_revision)?;
                let block = match p.kind.as_str() {
                    "mixer" => Block::Mixer,
                    "meter" => Block::Meter,
                    "capture" => Block::Capture { endpoint_id: None },
                    "output" => Block::Output { endpoint_id: None },
                    "gain" => Block::Gain {
                        gain_db: GainDb::try_from(0.0)?,
                        bypass: false,
                    },
                    "tone" => Block::Tone { enabled: true },
                    "plugin" => self
                        .backend
                        .plugin_block(
                            p.plugin_id
                                .as_deref()
                                .ok_or_else(|| ApiError::invalid("ID do plugin ausente."))?,
                        )
                        .map_err(|e| ApiError::invalid(e.to_string()))?,
                    _ => return Err(ApiError::invalid("Tipo de bloco desconhecido.")),
                };
                let id = NodeId(
                    self.backend
                        .new_id()
                        .map_err(|e| ApiError::new(ErrorCode::BackendError, e.to_string()))?,
                );
                let mut graph = self.graph.clone();
                graph.nodes.push(Node { id, block });
                self.apply(graph)
            }
            "audio.mute" => {
                let p: Mute = params(request.params)?;
                self.muted = p.muted;
                self.reconcile(false);
                Ok(self.snapshot())
            }
            "audio.retry" => {
                empty(request.params)?;
                self.suspended = false;
                self.reconcile(true);
                Ok(self.snapshot())
            }
            "system.shutdown" => {
                empty(request.params)?;
                self.stop()?;
                self.shutdown = true;
                Ok(json!({"accepted":true}))
            }
            _ => Err(ApiError::invalid(
                "Comando desconhecido no backend do aplicativo.",
            )),
        }
    }
}
fn params<T: serde::de::DeserializeOwned>(value: Value) -> Result<T, ApiError> {
    serde_json::from_value(value).map_err(|e| ApiError::invalid(e.to_string()))
}
fn empty(value: Value) -> Result<(), ApiError> {
    if value.as_object().is_some_and(|v| v.is_empty()) {
        Ok(())
    } else {
        Err(ApiError::invalid("Este comando não aceita parâmetros."))
    }
}
