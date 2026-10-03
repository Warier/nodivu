//! Owner-only control/telemetry. Never called by the WASAPI/DSP thread.
use super::*;
use nodivu_clap_host::{CaptureState, CaptureTarget};
use nodivu_core::capture::CaptureSelection;
use std::sync::{Mutex, atomic::AtomicBool};

struct Command {
    index: usize,
    mode: u32,
    executable: String,
    pid: u32,
    creation: u64,
}
pub(super) struct CaptureControls {
    supported: Vec<bool>,
    commands: Mutex<Vec<Command>>,
    states: Vec<Mutex<CaptureState>>,
    errors: Vec<Mutex<String>>,
    targets: Mutex<Result<Vec<CaptureTarget>, String>>,
    refresh: AtomicBool,
    root_pid: u32,
}
impl CaptureControls {
    pub fn prepare(owners: &[OfflinePlugin]) -> Result<Self, Error> {
        let root_pid = match std::env::var("NODIVU_APP_ROOT_PID") {
            Ok(value) => value
                .parse::<u32>()
                .ok()
                .filter(|p| *p != 0)
                .ok_or(Error::Contract("PID raiz do aplicativo inválido"))?,
            Err(std::env::VarError::NotPresent) => std::process::id(),
            Err(_) => return Err(Error::Contract("PID raiz não UTF-8")),
        };
        Ok(Self {
            supported: owners
                .iter()
                .map(|o| o.capture_snapshot().is_ok())
                .collect(),
            commands: Mutex::new(Vec::new()),
            states: owners
                .iter()
                .map(|_| Mutex::new(CaptureState::default()))
                .collect(),
            errors: owners.iter().map(|_| Mutex::new(String::new())).collect(),
            targets: Mutex::new(Ok(Vec::new())),
            refresh: AtomicBool::new(true),
            root_pid,
        })
    }
    pub(super) fn supports(&self, index: usize) -> bool {
        self.supported.get(index).copied().unwrap_or(false)
    }
    fn queue(&self, command: Command) -> Result<(), BackendError> {
        let mut queue = self
            .commands
            .lock()
            .map_err(|_| invalid("Controle de captura indisponível"))?;
        queue.retain(|c| c.index != command.index);
        queue.push(command); // at most one command per bounded native instance
        Ok(())
    }
    pub fn clear(&self, index: usize) -> Result<(), BackendError> {
        if self.supports(index) {
            self.queue(Command {
                index,
                mode: 0,
                executable: String::new(),
                pid: 0,
                creation: 0,
            })?;
        }
        Ok(())
    }
    pub fn state(&self, index: usize) -> Value {
        let mut value = self
            .states
            .get(index)
            .and_then(|s| s.lock().ok())
            .map_or(Value::Null, |state| json!(*state));
        if let Some(object) = value.as_object_mut()
            && let Some(error) = self.errors.get(index).and_then(|e| e.lock().ok())
        {
            object.insert("control_error".into(), json!(*error));
        }
        value
    }
    pub fn maintain(&self, owners: &[OfflinePlugin]) {
        if let Ok(mut commands) = self.commands.lock() {
            for c in commands.drain(..) {
                if let Some(owner) = owners.get(c.index) {
                    let result = owner.capture_configure(c.mode, &c.executable, c.pid, c.creation);
                    if let Ok(mut error) = self.errors[c.index].lock() {
                        *error = result.err().map_or(String::new(), |e| e.to_string());
                    }
                }
            }
        }
        for (i, owner) in owners.iter().enumerate() {
            if self.supports(i)
                && let Ok(state) = owner.capture_snapshot()
                && let Ok(mut target) = self.states[i].lock()
            {
                *target = state;
            }
        }
        if self.refresh.load(SeqCst) {
            let result = owners
                .iter()
                .enumerate()
                .find(|(i, _)| self.supports(*i))
                .ok_or_else(|| "Plugin de captura não instalado".to_owned())
                .and_then(|(_, owner)| owner.capture_targets().map_err(|e| e.to_string()));
            if let Ok(mut targets) = self.targets.lock() {
                *targets = result;
            }
            self.refresh.store(false, SeqCst);
        }
    }
}
impl PluginBackend {
    pub(super) fn capture_targets_impl(&self, refresh: bool) -> Result<Value, BackendError> {
        if refresh {
            self.controls.capture.refresh.store(true, SeqCst);
        }
        let targets = self
            .controls
            .capture
            .targets
            .lock()
            .map_err(|_| invalid("Catálogo de aplicativos indisponível"))?;
        match &*targets {
            Ok(targets) => {
                let values = targets.iter().map(|t| Ok(json!({"token":t.token(),"executable":t.executable()?,"label":t.label()?,"process_id":t.process_id}))).collect::<Result<Vec<_>, Error>>()
                    .map_err(|e| BackendError::new("catálogo de aplicativos", std::io::Error::other(e.to_string())))?;
                Ok(
                    json!({"targets":values,"refreshing":self.controls.capture.refresh.load(SeqCst)}),
                )
            }
            Err(error) if self.controls.capture.refresh.load(SeqCst) => {
                Ok(json!({"targets":[],"refreshing":true,"warning":error}))
            }
            Err(error) => Err(BackendError::new(
                "listar aplicativos",
                std::io::Error::other(error.clone()),
            )),
        }
    }
    pub(super) fn capture_configure_impl(
        &mut self,
        id: NodeId,
        selection: Option<CaptureSelection>,
        token: Option<&str>,
    ) -> Result<(), BackendError> {
        let index = self
            .slots
            .iter()
            .position(|n| *n == Some(id))
            .ok_or_else(|| invalid("Nó de captura ausente"))?;
        if !self.controls.capture.supports(index) {
            return Err(invalid("Plugin sem seleção de aplicativos"));
        }
        if let Some(selection) = &selection {
            selection.validate().map_err(|e| invalid_owned(e.message))?;
        }
        let (mode, executable) = match &selection {
            None => (0, String::new()),
            Some(CaptureSelection::Application { executable }) => (1, executable.clone()),
            Some(CaptureSelection::System {}) => (2, String::new()),
        };
        let (pid, creation) = if let Some(token) = token {
            if mode != 1 {
                return Err(invalid("Token só é válido para aplicativo"));
            }
            let targets = self
                .controls
                .capture
                .targets
                .lock()
                .map_err(|_| invalid("Catálogo indisponível"))?;
            let values = targets
                .as_ref()
                .map_err(|_| invalid("Atualize a lista de aplicativos"))?;
            let target = values
                .iter()
                .find(|t| t.token() == token)
                .ok_or_else(|| invalid("Aplicativo mudou; atualize a lista"))?;
            if target.executable().ok().as_deref() != Some(&executable) {
                return Err(invalid("Token não corresponde ao aplicativo"));
            }
            (target.process_id, target.creation_time)
        } else {
            (
                if mode == 2 {
                    self.controls.capture.root_pid
                } else {
                    0
                },
                0,
            )
        };
        self.controls.capture.queue(Command {
            index,
            mode,
            executable,
            pid,
            creation,
        })?;
        self.capture_selections[index] = selection;
        Ok(())
    }
}
fn invalid_owned(message: String) -> BackendError {
    BackendError::new(
        "selecionar aplicativo",
        std::io::Error::new(std::io::ErrorKind::InvalidInput, message),
    )
}
