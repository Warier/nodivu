//! Única borda FFI: binários conformes/trusted, ponteiros privados e lifetime do módulo explícito.
#[path = "capture_source.rs"]
mod capture_source;
pub use capture_source::{CaptureState, CaptureTarget};
use clap_sys::{
    audio_buffer::clap_audio_buffer,
    entry::clap_plugin_entry,
    events::*,
    ext::{audio_ports::*, latency::*, params::*, thread_check::*},
    factory::plugin_factory::{CLAP_PLUGIN_FACTORY_ID, clap_plugin_factory},
    host::clap_host,
    id::CLAP_INVALID_ID,
    plugin::clap_plugin,
    process::*,
    version::{CLAP_VERSION, clap_version_is_compatible},
};
use nodivu_block::{MAX_EVENTS, MAX_FRAMES, ParameterEvent, StereoMut};
use serde::Serialize;
use std::{
    ffi::{CStr, CString, c_char, c_void},
    marker::PhantomData,
    os::windows::ffi::OsStrExt,
    path::Path,
    ptr,
    rc::Rc,
    sync::atomic::{AtomicBool, AtomicU32, Ordering::Relaxed},
    thread::ThreadId,
};
use windows::{
    Win32::{
        Foundation::{FreeLibrary, HMODULE},
        System::LibraryLoader::*,
    },
    core::{PCSTR, PCWSTR},
};

#[derive(Debug)]
pub enum Error {
    Io(std::io::Error),
    Windows(windows::core::Error),
    Contract(&'static str),
    ProcessStatus(i32),
}
impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Io(e) => write!(f, "Arquivo do plugin: {e}"),
            Self::Windows(e) => write!(f, "Biblioteca do plugin: {e}"),
            Self::Contract(e) => f.write_str(e),
            Self::ProcessStatus(status) => write!(
                f,
                "Plugin retornou status de processamento inválido/falha: {status}"
            ),
        }
    }
}
impl std::error::Error for Error {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            Self::Io(e) => Some(e),
            Self::Windows(e) => Some(e),
            _ => None,
        }
    }
}
type Result<T> = std::result::Result<T, Error>;
fn required<T>(value: Option<T>) -> Result<T> {
    value.ok_or(Error::Contract("Callback CLAP obrigatório ausente"))
}

struct Library(HMODULE);
impl Drop for Library {
    fn drop(&mut self) {
        // SAFETY: handle adquirido por LoadLibraryExW; instances/entry já destruídos pelo owner.
        let _ = unsafe { FreeLibrary(self.0) };
    }
}
struct Entry {
    pointer: *const clap_plugin_entry,
    initialized: bool,
    _library: Library,
}
impl Drop for Entry {
    fn drop(&mut self) {
        if self.initialized {
            // SAFETY: init teve sucesso, nenhum plugin sobrevive ao Entry e módulo segue carregado.
            unsafe {
                if let Some(deinit) = (*self.pointer).deinit {
                    deinit();
                }
            }
        }
    }
}

struct Context {
    owner: ThreadId,
    audio: AtomicU32,
    cancel: AtomicBool,
    callback: AtomicBool,
    restart: AtomicBool,
    wake: AtomicBool,
}
// SAFETY: callbacks recebem somente o host estável privado fornecido à instância;
// seu contexto permanece vivo até depois de destroy (que deve encerrar threads do plugin).
unsafe fn context<'a>(host: *const clap_host) -> &'a Context {
    // SAFETY: host/context privados estáveis, conforme pré-condição descrita acima.
    unsafe { &*((*host).host_data.cast::<Context>()) }
}
unsafe extern "C" fn is_main(host: *const clap_host) -> bool {
    // SAFETY: contrato comum de callback acima; só leitura/atômicos.
    unsafe { context(host).owner == std::thread::current().id() }
}
unsafe extern "C" fn is_audio(host: *const clap_host) -> bool {
    // SAFETY: callback válido; a função é true somente para o owner dentro da chamada DSP.
    unsafe {
        context(host).audio.load(Relaxed) == windows::Win32::System::Threading::GetCurrentThreadId()
    }
}
unsafe extern "C" fn callback(host: *const clap_host) {
    // SAFETY: contexto válido; pedido de qualquer thread, coalescido sem alocação.
    unsafe {
        context(host).callback.store(true, Relaxed);
    }
}
unsafe extern "C" fn restart(host: *const clap_host) {
    // SAFETY: contexto válido durante lifetime do plugin.
    unsafe {
        context(host).restart.store(true, Relaxed);
    }
}
unsafe extern "C" fn wake(host: *const clap_host) {
    // SAFETY: contexto válido durante lifetime do plugin.
    unsafe {
        context(host).wake.store(true, Relaxed);
    }
}
static THREAD_CHECK: clap_host_thread_check = clap_host_thread_check {
    is_main_thread: Some(is_main),
    is_audio_thread: Some(is_audio),
};
unsafe extern "C" fn extension(_: *const clap_host, id: *const c_char) -> *const c_void {
    if id.is_null() {
        return ptr::null();
    }
    // SAFETY: ID de extensão CLAP é C string válida durante callback.
    if unsafe { CStr::from_ptr(id) } == CLAP_EXT_THREAD_CHECK {
        (&THREAD_CHECK as *const clap_host_thread_check).cast()
    } else {
        ptr::null()
    }
}

#[derive(Debug, Serialize)]
pub struct ParameterInfo {
    pub id: u32,
    pub name: String,
    pub min: f64,
    pub max: f64,
    pub default: f64,
    pub readonly: bool,
    pub stepped: bool,
}
#[derive(Debug, Serialize)]
pub struct Metadata {
    pub id: String,
    pub name: String,
    pub has_input: bool,
    pub has_output: bool,
    pub supports_in_place: bool,
    pub latency_frames: u32,
    pub parameters: Vec<ParameterInfo>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProcessStatus {
    Continue,
    ContinueIfNotQuiet,
    Tail,
    Sleep,
}

/// Sessão offline na thread principal do processo. Não é ainda um worker WASAPI.
/// O módulo é mantido carregado até stop/deactivate/destroy/deinit terminarem.
pub struct OfflinePlugin {
    plugin: *const clap_plugin,
    active: bool,
    processing: bool,
    metadata: Metadata,
    context: Box<Context>,
    _host: Box<clap_host>,
    _entry: Rc<Entry>,
    _not_send_sync: PhantomData<Rc<()>>,
}
impl Drop for OfflinePlugin {
    fn drop(&mut self) {
        // SAFETY: ponteiro validado na criação; owner não é Send/Sync; módulo/contexto vivos.
        unsafe {
            if self.processing {
                self.context.audio.store(
                    windows::Win32::System::Threading::GetCurrentThreadId(),
                    Relaxed,
                );
                if let Some(stop) = (*self.plugin).stop_processing {
                    stop(self.plugin);
                }
                self.context.audio.store(0, Relaxed);
            }
            if self.active
                && let Some(f) = (*self.plugin).deactivate
            {
                f(self.plugin);
            }
            if let Some(f) = (*self.plugin).destroy {
                f(self.plugin);
            }
        }
        // Fields só são destruídos após o corpo: plugin já não pode chamar o host.
    }
}

impl OfflinePlugin {
    /// Main mantém o transporte durante toda a vida do worker. O cancelamento precede join.
    pub fn with_audio_main<T: Send, U>(
        &mut self,
        work: impl FnOnce(&mut AudioProcessor<'_>) -> Result<T> + Send,
        main: impl FnOnce(&Self) -> U,
    ) -> Result<(T, U)> {
        self.activate_only()?;
        self.context.cancel.store(false, Relaxed);
        let result = std::thread::scope(|scope| {
            struct Cancel<'a>(&'a AtomicBool);
            impl Drop for Cancel<'_> {
                fn drop(&mut self) {
                    self.0.store(true, Relaxed);
                }
            }
            let cancel = Cancel(&self.context.cancel);
            let mut audio = self.audio_view(true);
            let worker = std::thread::Builder::new()
                .name("clap-wasapi".into())
                .spawn_scoped(scope, move || {
                    let _finished = Cancel(&audio.context.cancel);
                    audio.start()?;
                    work(&mut audio)
                })
                .map_err(Error::Io)?;
            let value = main(self);
            drop(cancel);
            let audio_result = worker
                .join()
                .map_err(|_| Error::Contract("Worker CLAP terminou inesperadamente"))??;
            Ok((audio_result, value))
        });
        // SAFETY: scope já confirmou join/stop, owner volta a ter exclusividade.
        unsafe {
            required((*self.plugin).deactivate)?(self.plugin);
        }
        self.active = false;
        result
    }
    /// One symbolic audio thread for all instances; no queue/copy between plugin calls.
    /// Main-thread owners remain !Send, shared module entry outlives every processor token.
    pub fn with_audio_group<T: Send, U>(
        owners: &mut [Self],
        work: impl FnOnce(&mut [AudioProcessor<'_>]) -> Result<T> + Send,
        main: impl FnOnce(&[Self]) -> U,
    ) -> Result<(T, U)> {
        if owners.is_empty() || owners.iter().any(|o| o.active) {
            return Err(Error::Contract("Grupo vazio ou já ativo"));
        }
        let result = (|| {
            for owner in owners.iter_mut() {
                owner.activate_only()?;
                owner.context.cancel.store(false, Relaxed);
            }
            std::thread::scope(|scope| {
                struct Cancel<'a>(Vec<&'a AtomicBool>);
                impl Drop for Cancel<'_> {
                    fn drop(&mut self) {
                        for flag in &self.0 {
                            flag.store(true, Relaxed);
                        }
                    }
                }
                let flags = || owners.iter().map(|o| &o.context.cancel).collect();
                let cancel = Cancel(flags());
                let finished = Cancel(flags());
                let mut audio: Vec<_> = owners.iter().map(|o| o.audio_view(true)).collect();
                let worker = std::thread::Builder::new()
                    .name("clap-chain-wasapi".into())
                    .spawn_scoped(scope, move || {
                        let _finished = finished;
                        for processor in &mut audio {
                            processor.start()?;
                        }
                        work(&mut audio)
                    })
                    .map_err(Error::Io)?;
                let value = main(owners);
                drop(cancel);
                let result = worker
                    .join()
                    .map_err(|_| Error::Contract("Worker CLAP terminou inesperadamente"))??;
                Ok((result, value))
            })
        })();
        // SAFETY: scope joined and dropped every token, including partial start failures.
        // Also rolls back partial activation on the main thread.
        for owner in owners.iter_mut().filter(|o| o.active) {
            // SAFETY: scoped worker joined; no AudioProcessor borrows remain, owner is active.
            unsafe {
                required((*owner.plugin).deactivate)?(owner.plugin);
            }
            owner.active = false;
        }
        result
    }
    pub fn worker_cancelled(&self) -> bool {
        self.context.cancel.load(Relaxed)
    }
    /// Carrega somente um caminho explícito, sem varrer diretórios ou alterar PATH global.
    ///
    /// # Safety
    /// Deve ser chamado na thread principal. O binário e suas dependências devem ser confiáveis,
    /// cumprir CLAP (incluindo validade de ponteiros, callbacks e ausência de unwinding pela ABI),
    /// e encerrar callbacks/threads antes de destroy. Validação de metadados não isola código nativo.
    pub unsafe fn load(path: &Path, id: &str) -> Result<Self> {
        let path = path.canonicalize().map_err(Error::Io)?;
        let utf8 = path
            .to_str()
            .ok_or(Error::Contract("Caminho CLAP precisa ser UTF-8"))?;
        let cpath = CString::new(utf8).map_err(|_| Error::Contract("Caminho contém NUL"))?;
        let id = CString::new(id).map_err(|_| Error::Contract("ID contém NUL"))?;
        let wide: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        // SAFETY: caminho absoluto NUL-terminado; busca de dependências limitada à pasta e System32.
        let library = Library(
            unsafe {
                LoadLibraryExW(
                    PCWSTR(wide.as_ptr()),
                    None,
                    LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32,
                )
            }
            .map_err(Error::Windows)?,
        );
        // SAFETY: símbolo de dados CLAP em módulo vivo; contrato unsafe de load garante ABI válida.
        let address = unsafe { GetProcAddress(library.0, PCSTR(c"clap_entry".as_ptr().cast())) }
            .ok_or(Error::Contract("Símbolo clap_entry ausente"))?;
        let mut entry = Entry {
            pointer: address as *const () as *const clap_plugin_entry,
            initialized: false,
            _library: library,
        };
        // SAFETY: toda leitura/call a seguir usa a ABI do módulo confiável, no owner,
        // com strings/host estáveis; Entry faz cleanup de cada init bem-sucedido.
        unsafe {
            let e = &*entry.pointer;
            if !clap_version_is_compatible(e.clap_version) {
                return Err(Error::Contract("ABI CLAP incompatível"));
            }
            let init = required(e.init)?;
            required(e.deinit)?;
            required(e.get_factory)?;
            if !init(cpath.as_ptr()) {
                return Err(Error::Contract("entry.init recusou plugin"));
            }
            entry.initialized = true;
            Self::from_entry(Rc::new(entry), &id)
        }
    }
    /// Creates independent state from the same initialized module, on its owner thread.
    pub fn new_instance(&self) -> Result<Self> {
        let id =
            CString::new(self.metadata.id.as_str()).map_err(|_| Error::Contract("ID inválido"))?;
        // SAFETY: same trusted module and owner; Rc keeps entry alive through every instance.
        unsafe { Self::from_entry(self._entry.clone(), &id) }
    }
    unsafe fn from_entry(entry: Rc<Entry>, id: &CStr) -> Result<Self> {
        // SAFETY: initialized CLAP entry, owner thread, trusted ABI and stable strings/host.
        unsafe {
            let factory_fn = required((*entry.pointer).get_factory)?;
            let factory = factory_fn(CLAP_PLUGIN_FACTORY_ID.as_ptr()).cast::<clap_plugin_factory>();
            if factory.is_null() {
                return Err(Error::Contract("Factory CLAP ausente"));
            }
            let factory = &*factory;
            let count = required(factory.get_plugin_count)?(factory);
            if !(1..=64).contains(&count) {
                return Err(Error::Contract("Quantidade de tipos fora do limite 1..64"));
            }
            let descriptor = required(factory.get_plugin_descriptor)?;
            let mut matches = 0;
            let mut name = String::new();
            for index in 0..count {
                let d = descriptor(factory, index);
                if d.is_null() {
                    return Err(Error::Contract("Descritor nulo"));
                }
                let d = &*d;
                if !clap_version_is_compatible(d.clap_version) {
                    return Err(Error::Contract("Tipo com ABI incompatível"));
                }
                if text(d.id)? == id.to_str().map_err(|_| Error::Contract("ID inválido"))? {
                    matches += 1;
                    name = text(d.name)?;
                }
            }
            if matches != 1 {
                return Err(Error::Contract("ID ausente ou ambíguo no módulo"));
            }
            let mut context = Box::new(Context {
                owner: std::thread::current().id(),
                audio: AtomicU32::new(0),
                cancel: AtomicBool::new(false),
                callback: AtomicBool::new(false),
                restart: AtomicBool::new(false),
                wake: AtomicBool::new(false),
            });
            let host = Box::new(clap_host {
                clap_version: CLAP_VERSION,
                host_data: (&mut *context as *mut Context).cast(),
                name: c"Nodivu offline".as_ptr(),
                vendor: c"Nodivu".as_ptr(),
                url: c"".as_ptr(),
                version: c"0.1.0".as_ptr(),
                get_extension: Some(extension),
                request_restart: Some(restart),
                request_process: Some(wake),
                request_callback: Some(callback),
            });
            let plugin = required(factory.create_plugin)?(factory, &*host, id.as_ptr());
            if plugin.is_null() {
                return Err(Error::Contract("Factory recusou instância"));
            }
            let mut instance = Self {
                plugin,
                active: false,
                processing: false,
                metadata: Metadata {
                    id: id.to_string_lossy().into_owned(),
                    name,
                    has_input: false,
                    has_output: true,
                    supports_in_place: false,
                    latency_frames: 0,
                    parameters: Vec::new(),
                },
                context,
                _host: host,
                _entry: entry,
                _not_send_sync: PhantomData,
            };
            let p = &*plugin;
            required(p.destroy)?;
            required(p.deactivate)?;
            required(p.stop_processing)?;
            required(p.reset)?;
            required(p.on_main_thread)?;
            required(p.process)?;
            required(p.activate)?;
            required(p.start_processing)?;
            required(p.get_extension)?;
            if !required(p.init)?(plugin) {
                return Err(Error::Contract("plugin.init recusou instância"));
            }
            instance.inspect()?;
            instance.service_main_thread()?;
            Ok(instance)
        }
    }
    /// Nodivu file-player/1 extension; main thread only, local path copied by plugin.
    pub fn file_command(&self, action: &str, path: Option<&str>) -> Result<i32> {
        #[repr(C)]
        struct Files {
            load: Option<unsafe extern "C" fn(*const clap_plugin, *const c_char) -> bool>,
            play: Option<unsafe extern "C" fn(*const clap_plugin) -> bool>,
            status: Option<unsafe extern "C" fn(*const clap_plugin) -> i32>,
        }
        // SAFETY: trusted extension versioned ABI, live owner on main; no pointer escapes.
        unsafe {
            let ext = required((*self.plugin).get_extension)?(
                self.plugin,
                c"org.nodivu.file-player/1".as_ptr(),
            )
            .cast::<Files>();
            if ext.is_null() {
                return Err(Error::Contract("Plugin sem controle de arquivo"));
            }
            match action {
                "load" | "clear" => {
                    let path = path
                        .map(CString::new)
                        .transpose()
                        .map_err(|_| Error::Contract("Caminho contém NUL"))?;
                    if !required((*ext).load)?(
                        self.plugin,
                        path.as_ref().map_or(ptr::null(), |p| p.as_ptr()),
                    ) {
                        return Err(Error::Contract("Arquivo recusado"));
                    }
                }
                "play" => {
                    if !required((*ext).play)?(self.plugin) {
                        return Err(Error::Contract("Player ainda não está pronto"));
                    }
                }
                "status" => {}
                _ => return Err(Error::Contract("Ação desconhecida")),
            }
            Ok(required((*ext).status)?(self.plugin))
        }
    }
    /// Optional file-transport/1; owner/main thread only. Times are milliseconds.
    pub fn file_transport(
        &self,
        action: &str,
        position_ms: Option<u32>,
    ) -> Result<(u32, u32, i32)> {
        #[repr(C)]
        struct Transport {
            play: Option<unsafe extern "C" fn(*const clap_plugin) -> bool>,
            pause: Option<unsafe extern "C" fn(*const clap_plugin) -> bool>,
            seek: Option<unsafe extern "C" fn(*const clap_plugin, u32) -> bool>,
            position: Option<unsafe extern "C" fn(*const clap_plugin) -> u32>,
            duration: Option<unsafe extern "C" fn(*const clap_plugin) -> u32>,
            status: Option<unsafe extern "C" fn(*const clap_plugin) -> i32>,
        }
        // SAFETY: versioned trusted C ABI on a live plugin; main-thread-only calls;
        // pointers stay borrowed until the call returns. Telemetry is scalar.
        unsafe {
            let ext = required((*self.plugin).get_extension)?(
                self.plugin,
                c"org.nodivu.file-transport/1".as_ptr(),
            )
            .cast::<Transport>();
            if ext.is_null() {
                return Err(Error::Contract("Plugin sem transporte de arquivo"));
            }
            let accepted = match action {
                "play" => required((*ext).play)?(self.plugin),
                "pause" => required((*ext).pause)?(self.plugin),
                "seek" => required((*ext).seek)?(
                    self.plugin,
                    position_ms.ok_or(Error::Contract("Posição ausente"))?,
                ),
                "status" => true,
                _ => return Err(Error::Contract("Transporte desconhecido")),
            };
            if !accepted {
                return Err(Error::Contract("Player recusou transporte"));
            }
            Ok((
                required((*ext).position)?(self.plugin),
                required((*ext).duration)?(self.plugin),
                required((*ext).status)?(self.plugin),
            ))
        }
    }
    pub fn metadata(&self) -> &Metadata {
        &self.metadata
    }

    // SAFETY: chamador usa somente instância inicializada/inativa no owner; retornos
    // de extensões/strings pertencem ao módulo e são usados antes de unload.
    unsafe fn inspect(&mut self) -> Result<()> {
        // SAFETY: instância inicializada/inativa e owner exclusivo, conforme contrato do método.
        unsafe {
            let get = required((*self.plugin).get_extension)?;
            let ports =
                get(self.plugin, CLAP_EXT_AUDIO_PORTS.as_ptr()).cast::<clap_plugin_audio_ports>();
            if ports.is_null() {
                return Err(Error::Contract("Plugin sem portas de áudio"));
            }
            let ports = &*ports;
            let count = required(ports.count)?;
            let inputs = count(self.plugin, true);
            let outputs = count(self.plugin, false);
            if inputs > 1 || outputs > 1 || inputs + outputs == 0 {
                return Err(Error::Contract(
                    "Perfil exige fonte, efeito ou consumidor com um bus por direção",
                ));
            }
            let mut input: clap_audio_port_info = std::mem::zeroed();
            let mut output: clap_audio_port_info = std::mem::zeroed();
            let info = required(ports.get)?;
            if outputs == 1 && !info(self.plugin, 0, false, &mut output) {
                return Err(Error::Contract("Porta de saída inválida"));
            }
            if outputs == 1 {
                validate_port(&output)?;
            }
            if inputs == 1 {
                if !info(self.plugin, 0, true, &mut input) {
                    return Err(Error::Contract("Porta de entrada inválida"));
                }
                validate_port(&input)?;
            }
            self.metadata.has_input = inputs == 1;
            self.metadata.has_output = outputs == 1;
            self.metadata.supports_in_place = inputs == 1
                && outputs == 1
                && input.in_place_pair == output.id
                && output.in_place_pair == input.id;
            let params = get(self.plugin, CLAP_EXT_PARAMS.as_ptr()).cast::<clap_plugin_params>();
            if !params.is_null() {
                let count = required((*params).count)?(self.plugin);
                if count > 64 {
                    return Err(Error::Contract("Mais de 64 parâmetros"));
                }
                for index in 0..count {
                    let mut p: clap_param_info = std::mem::zeroed();
                    if !required((*params).get_info)?(self.plugin, index, &mut p) {
                        return Err(Error::Contract("Parâmetro inválido"));
                    }
                    if p.id == CLAP_INVALID_ID
                        || self.metadata.parameters.iter().any(|a| a.id == p.id)
                        || !p.min_value.is_finite()
                        || !p.max_value.is_finite()
                        || !p.default_value.is_finite()
                        || !(p.min_value..=p.max_value).contains(&p.default_value)
                    {
                        return Err(Error::Contract("ID ou faixa de parâmetro inválidos"));
                    }
                    let bytes: Vec<u8> = p.name.iter().map(|c| *c as u8).collect();
                    let name = CStr::from_bytes_until_nul(&bytes)
                        .map_err(|_| Error::Contract("Nome de parâmetro sem NUL"))?
                        .to_str()
                        .map_err(|_| Error::Contract("Nome de parâmetro não UTF-8"))?
                        .to_owned();
                    self.metadata.parameters.push(ParameterInfo {
                        id: p.id,
                        name,
                        min: p.min_value,
                        max: p.max_value,
                        default: p.default_value,
                        readonly: p.flags & CLAP_PARAM_IS_READONLY != 0,
                        stepped: p.flags & CLAP_PARAM_IS_STEPPED != 0,
                    });
                }
            }
            Ok(())
        }
    }
    fn activate_only(&mut self) -> Result<()> {
        if self.active {
            return Err(Error::Contract("Plugin já ativo"));
        }
        // SAFETY: owner, instância inativa; callbacks obrigatórios validados em load.
        unsafe {
            if !required((*self.plugin).activate)?(self.plugin, 48_000.0, 1, MAX_FRAMES as u32) {
                return Err(Error::Contract("Ativação recusada"));
            }
            self.active = true;
            let ext =
                required((*self.plugin).get_extension)?(self.plugin, CLAP_EXT_LATENCY.as_ptr())
                    .cast::<clap_plugin_latency>();
            if !ext.is_null() {
                self.metadata.latency_frames = required((*ext).get)?(self.plugin);
            }
        }
        Ok(())
    }
    pub fn activate(&mut self) -> Result<()> {
        self.activate_only()?;
        {
            let mut audio = self.audio_view(false);
            audio.start()?;
        }
        self.processing = true;
        Ok(())
    }
    fn audio_view(&self, owns_processing: bool) -> AudioProcessor<'_> {
        AudioProcessor {
            plugin: self.plugin,
            context: &self.context,
            metadata: &self.metadata,
            processing: self.processing,
            owns_processing,
            _not_sync: PhantomData,
        }
    }
    /// Ativa no owner, empresta DSP exclusivamente ao worker e recolhe antes de desativar.
    /// O trabalho deve terminar ou consultar cancelled(); DLL travada não pode ser interrompida.
    /// Retorna resultado e quantidade de callbacks main atendidos durante o escopo.
    pub fn with_audio_worker<T: Send>(
        &mut self,
        work: impl FnOnce(&mut AudioProcessor<'_>) -> Result<T> + Send,
    ) -> Result<(T, u64)> {
        self.with_audio_worker_control(work, || Ok(()))
    }
    /// Manutenção limitada no owner enquanto o worker vive; não executar comandos bloqueantes.
    /// Ao pedir encerramento, control também deve sinalizar qualquer loop externo de hardware.
    pub fn with_audio_worker_control<T: Send>(
        &mut self,
        work: impl FnOnce(&mut AudioProcessor<'_>) -> Result<T> + Send,
        mut control: impl FnMut() -> Result<()>,
    ) -> Result<(T, u64)> {
        self.activate_only()?;
        self.context.cancel.store(false, Relaxed);
        let result = std::thread::scope(|scope| {
            struct CancelOnDrop<'a>(&'a AtomicBool);
            impl Drop for CancelOnDrop<'_> {
                fn drop(&mut self) {
                    self.0.store(true, Relaxed);
                }
            }
            // Se control desenrolar a pilha, scope ainda precisa recolher o worker.
            // Sinalizar antes do join permite ao trabalho cooperativo encerrar nesse caminho.
            let _cancel_on_exit = CancelOnDrop(&self.context.cancel);
            let mut audio = self.audio_view(true);
            let worker = std::thread::Builder::new()
                .name("clap-dsp-test".into())
                .spawn_scoped(scope, move || {
                    audio.start()?;
                    work(&mut audio)
                    // AudioProcessor::drop faz stop, nunca destroy/unload.
                })
                .map_err(Error::Io)?;
            let mut failure = None;
            let mut callbacks = 0;
            while !worker.is_finished() {
                if let Err(error) = control() {
                    failure.get_or_insert(error);
                    self.context.cancel.store(true, Relaxed);
                }
                if !crate::pump_main_thread() {
                    failure.get_or_insert(Error::Contract("Encerramento do ciclo nativo"));
                    self.context.cancel.store(true, Relaxed);
                }
                match self.service_main_thread() {
                    Ok(serviced) => callbacks += u64::from(serviced),
                    Err(error) => {
                        failure.get_or_insert(error);
                        self.context.cancel.store(true, Relaxed);
                    }
                }
                // Espera somente no owner offline, nunca dentro de process.
                std::thread::sleep(std::time::Duration::from_millis(1));
            }
            let joined = worker
                .join()
                .map_err(|_| Error::Contract("Worker CLAP terminou inesperadamente"));
            match self.service_main_thread() {
                Ok(serviced) => callbacks += u64::from(serviced),
                Err(error) => {
                    failure.get_or_insert(error);
                }
            }
            if let Some(error) = failure {
                return Err(error);
            }
            joined?.map(|value| (value, callbacks))
        });
        // SAFETY: scope confirmou join e stop em todos os caminhos, owner retomou exclusividade.
        unsafe {
            required((*self.plugin).deactivate)?(self.plugin);
        }
        self.active = false;
        result
    }
    pub fn process_in_place(
        &mut self,
        audio: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<ProcessStatus> {
        self.audio_view(false).process_in_place(audio, events)
    }
    pub fn process(
        &mut self,
        input: Option<(&[f32], &[f32])>,
        output: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<ProcessStatus> {
        self.audio_view(false).process(input, output, events)
    }
    /// Executa um callback pendente fora do trecho DSP; novos pedidos ficam para próximo ciclo.
    /// Reativação é sinalizada como erro nesta fatia, nunca ignorada silenciosamente.
    pub fn service_main_thread(&self) -> Result<bool> {
        let serviced = self.context.callback.swap(false, Relaxed);
        if serviced {
            // SAFETY: owner, fora do papel audio; contexto/instância vivos.
            unsafe {
                required((*self.plugin).on_main_thread)?(self.plugin);
            }
        }
        if self.context.restart.load(Relaxed) {
            return Err(Error::Contract(
                "Plugin solicitou reativação; não suportada neste harness",
            ));
        }
        self.context.wake.store(false, Relaxed); // Harness alimenta todos os blocos, inclusive após Sleep.
        Ok(serviced)
    }
}

/// Empréstimo de DSP válido apenas durante o escopo controlado pelo owner.
/// Não possui DLL/host nem permite deactivate/destroy no worker.
pub struct AudioProcessor<'a> {
    plugin: *const clap_plugin,
    context: &'a Context,
    metadata: &'a Metadata,
    processing: bool,
    owns_processing: bool,
    _not_sync: PhantomData<std::cell::Cell<()>>,
}
// SAFETY: CLAP permite transferir o papel simbólico audio entre threads. Só este token
// não clonável chama DSP; &mut controla exclusividade. Metadata é imutável, Context usa
// atômicos. O lifetime é emprestado do owner !Send e scope faz join antes de destroy.
// Main só pode executar callbacks CLAP explicitamente permitidos concomitantemente.
unsafe impl Send for AudioProcessor<'_> {}
impl Drop for AudioProcessor<'_> {
    fn drop(&mut self) {
        if self.owns_processing && self.processing {
            // SAFETY: token exclusivo e iniciado; stop permitido na thread simbólica atual.
            unsafe {
                self.context.audio.store(
                    windows::Win32::System::Threading::GetCurrentThreadId(),
                    Relaxed,
                );
                if let Some(stop) = (*self.plugin).stop_processing {
                    stop(self.plugin);
                }
                self.context.audio.store(0, Relaxed);
            }
        }
    }
}
impl AudioProcessor<'_> {
    pub fn reset(&mut self) -> Result<()> {
        if !self.processing {
            return Err(Error::Contract("Reset exige processamento ativo"));
        }
        // SAFETY: token exclusivo, instância ativa/viva, reset CLAP no papel audio.
        unsafe {
            self.context.audio.store(
                windows::Win32::System::Threading::GetCurrentThreadId(),
                Relaxed,
            );
            required((*self.plugin).reset)?(self.plugin);
            self.context.audio.store(0, Relaxed);
        }
        Ok(())
    }
    /// Metadados já inspecionados/ativados; empréstimo sem cópia ou alocação.
    pub fn metadata(&self) -> &Metadata {
        self.metadata
    }
    pub fn cancelled(&self) -> bool {
        self.context.cancel.load(Relaxed)
    }
    fn start(&mut self) -> Result<()> {
        // SAFETY: owner ativou a instância; token ainda não iniciou processamento.
        unsafe {
            self.context.audio.store(
                windows::Win32::System::Threading::GetCurrentThreadId(),
                Relaxed,
            );
            let started = required((*self.plugin).start_processing)?(self.plugin);
            self.context.audio.store(0, Relaxed);
            if !started {
                return Err(Error::Contract("Início do processamento recusado"));
            }
        }
        self.processing = true;
        Ok(())
    }
    /// Usa os canais do chamador diretamente quando o plugin declarou o par in-place.
    pub fn process_in_place(
        &mut self,
        audio: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<ProcessStatus> {
        if !self.metadata.supports_in_place {
            return Err(Error::Contract("Plugin não permite in-place"));
        }
        let frames = self.validate(audio.left.len(), audio.right.len(), events)?;
        let pointers = [audio.left.as_mut_ptr(), audio.right.as_mut_ptr()];
        // SAFETY: empréstimos exclusivos disjuntos, mesmo comprimento; plugin negociou alias in-place.
        unsafe { self.process_raw(Some(pointers), pointers, frames, events) }
    }
    /// Buffers distintos: entrada somente leitura, saída disjunta. Fonte usa input=None.
    pub fn process(
        &mut self,
        input: Option<(&[f32], &[f32])>,
        output: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<ProcessStatus> {
        let frames = self.validate(output.left.len(), output.right.len(), events)?;
        if input.is_some() != self.metadata.has_input {
            return Err(Error::Contract("Entrada incompatível com as portas"));
        }
        if let Some((l, r)) = input
            && (l.len() != frames || r.len() != frames)
        {
            return Err(Error::Contract("Entrada com tamanho diferente"));
        }
        let input = input.map(|(l, r)| [l.as_ptr().cast_mut(), r.as_ptr().cast_mut()]);
        // SAFETY: referências Rust garantem disjunção; CLAP proíbe escrever no input separado.
        unsafe {
            self.process_raw(
                input,
                [output.left.as_mut_ptr(), output.right.as_mut_ptr()],
                frames,
                events,
            )
        }
    }
    fn validate(&self, left: usize, right: usize, events: &[ParameterEvent]) -> Result<usize> {
        if self.cancelled() {
            return Err(Error::Contract("Processamento CLAP cancelado pelo owner"));
        }
        if !self.processing {
            return Err(Error::Contract("Plugin não está processando"));
        }
        if left == 0 || left != right || left > MAX_FRAMES {
            return Err(Error::Contract("Buffer inválido"));
        }
        if events.len() > MAX_EVENTS {
            return Err(Error::Contract("Mais de 32 eventos"));
        }
        let mut previous = 0;
        for event in events {
            let p = self
                .metadata
                .parameters
                .iter()
                .find(|p| p.id == event.id.0)
                .ok_or(Error::Contract("Parâmetro desconhecido"))?;
            if event.frame_offset < previous
                || event.frame_offset as usize >= left
                || p.readonly
                || !event.value.is_finite()
                || !(p.min..=p.max).contains(&event.value)
                || (p.stepped && event.value.fract() != 0.0)
            {
                return Err(Error::Contract("Evento inválido"));
            }
            previous = event.frame_offset;
        }
        Ok(left)
    }
    // SAFETY: caller valida buffers/alias/frames; ponteiros valem somente nesta chamada.
    unsafe fn process_raw(
        &mut self,
        input: Option<[*mut f32; 2]>,
        mut output: [*mut f32; 2],
        frames: usize,
        events: &[ParameterEvent],
    ) -> Result<ProcessStatus> {
        // SAFETY: zero é válido para esses structs C com ponteiros/números; campos exigidos preenchidos abaixo.
        let mut event_data: [clap_event_param_value; MAX_EVENTS] = unsafe { std::mem::zeroed() };
        for (dest, e) in event_data.iter_mut().zip(events) {
            *dest = clap_event_param_value {
                header: clap_event_header {
                    size: std::mem::size_of::<clap_event_param_value>() as u32,
                    time: e.frame_offset,
                    space_id: CLAP_CORE_EVENT_SPACE_ID,
                    type_: CLAP_EVENT_PARAM_VALUE,
                    flags: 0,
                },
                param_id: e.id.0,
                cookie: ptr::null_mut(),
                note_id: -1,
                port_index: -1,
                channel: -1,
                key: -1,
                value: e.value,
            };
        }
        let list = EventList {
            data: &event_data,
            count: events.len() as u32,
        };
        let in_events = clap_input_events {
            ctx: (&list as *const EventList<'_>).cast_mut().cast(),
            size: Some(event_count),
            get: Some(event_get),
        };
        let out_events = clap_output_events {
            ctx: ptr::null_mut(),
            try_push: Some(reject_event),
        };
        let mut input_pointers = input.unwrap_or([ptr::null_mut(); 2]);
        let input_buffer = clap_audio_buffer {
            data32: input_pointers.as_mut_ptr(),
            data64: ptr::null_mut(),
            channel_count: 2,
            latency: 0,
            constant_mask: 0,
        };
        let mut output_buffer = clap_audio_buffer {
            data32: output.as_mut_ptr(),
            data64: ptr::null_mut(),
            channel_count: 2,
            latency: 0,
            constant_mask: 0,
        };
        let process = clap_process {
            steady_time: -1,
            frames_count: frames as u32,
            transport: ptr::null(),
            audio_inputs: if input.is_some() {
                &input_buffer
            } else {
                ptr::null()
            },
            audio_outputs: if self.metadata.has_output {
                &mut output_buffer
            } else {
                ptr::null_mut()
            },
            audio_inputs_count: u32::from(input.is_some()),
            audio_outputs_count: u32::from(self.metadata.has_output),
            in_events: &in_events,
            out_events: &out_events,
        };
        // SAFETY: leitura da identidade da thread atual, sem acesso a memória externa.
        self.context.audio.store(
            unsafe { windows::Win32::System::Threading::GetCurrentThreadId() },
            Relaxed,
        );
        // SAFETY: buffers/list/host/module viventes, único owner e processamento iniciado.
        let status = unsafe { required((*self.plugin).process)?(self.plugin, &process) };
        self.context.audio.store(0, Relaxed);
        let result = match status {
            CLAP_PROCESS_CONTINUE => Ok(ProcessStatus::Continue),
            CLAP_PROCESS_CONTINUE_IF_NOT_QUIET => Ok(ProcessStatus::ContinueIfNotQuiet),
            CLAP_PROCESS_TAIL => Ok(ProcessStatus::Tail),
            CLAP_PROCESS_SLEEP => Ok(ProcessStatus::Sleep),
            status => Err(Error::ProcessStatus(status)),
        };
        if !self.metadata.has_output {
            return result;
        }
        // SAFETY: não existem referências Rust ativas aos canais durante a call FFI;
        // mesmos ponteiros disjuntos e comprimento validado pelo chamador.
        unsafe {
            let l = std::slice::from_raw_parts_mut(output[0], frames);
            let r = std::slice::from_raw_parts_mut(output[1], frames);
            if result.is_err() {
                l.fill(0.0);
                r.fill(0.0);
                return result;
            }
            if l.iter().chain(r.iter()).any(|v| !v.is_finite()) {
                l.fill(0.0);
                r.fill(0.0);
                return Err(Error::Contract(
                    "Plugin produziu amostra não finita; saída silenciada",
                ));
            }
        }
        result
    }
}

struct EventList<'a> {
    data: &'a [clap_event_param_value],
    count: u32,
}
unsafe extern "C" fn event_count(list: *const clap_input_events) -> u32 {
    // SAFETY: ctx aponta para EventList vivo na pilha durante process.
    unsafe { (*((*list).ctx.cast::<EventList<'_>>())).count }
}
unsafe extern "C" fn event_get(
    list: *const clap_input_events,
    index: u32,
) -> *const clap_event_header {
    // SAFETY: ctx tem lifetime de process; limites checados antes de devolver ponteiro.
    unsafe {
        let list = &*((*list).ctx.cast::<EventList<'_>>());
        if index < list.count {
            &list.data[index as usize].header
        } else {
            ptr::null()
        }
    }
}
unsafe extern "C" fn reject_event(
    _: *const clap_output_events,
    _: *const clap_event_header,
) -> bool {
    false
}

// SAFETY: plugin confiável fornece string válida até NUL; limite evita metadados enormes.
unsafe fn text(pointer: *const c_char) -> Result<String> {
    if pointer.is_null() {
        return Err(Error::Contract("Texto obrigatório nulo"));
    }
    // SAFETY: string de metadados do plugin, módulo ainda vivo.
    let bytes = unsafe { CStr::from_ptr(pointer) }.to_bytes();
    if bytes.is_empty() || bytes.len() > 4096 {
        return Err(Error::Contract("Texto vazio ou longo demais"));
    }
    String::from_utf8(bytes.to_vec()).map_err(|_| Error::Contract("Texto não UTF-8"))
}
// SAFETY: port_type válido/nulo conforme contrato CLAP.
unsafe fn validate_port(port: &clap_audio_port_info) -> Result<()> {
    if port.id == CLAP_INVALID_ID
        || port.channel_count != 2
        || port.flags & CLAP_AUDIO_PORT_IS_MAIN == 0
        || port.port_type.is_null()
    {
        return Err(Error::Contract("Perfil exige porta principal estéreo"));
    }
    // SAFETY: string de tipo válida pertencente à biblioteca viva.
    if unsafe { CStr::from_ptr(port.port_type) } != CLAP_PORT_STEREO {
        return Err(Error::Contract("Tipo de porta não estéreo"));
    }
    Ok(())
}
