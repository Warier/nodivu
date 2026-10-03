//! Optional owner-thread source-control ABI. No PCM crosses this boundary.
use super::*;

#[repr(C)]
#[derive(Clone)]
pub struct CaptureTarget {
    pub process_id: u32,
    reserved: u32,
    pub creation_time: u64,
    executable: [c_char; 4096],
    label: [c_char; 128],
}
impl Default for CaptureTarget {
    fn default() -> Self {
        Self {
            process_id: 0,
            reserved: 0,
            creation_time: 0,
            executable: [0; 4096],
            label: [0; 128],
        }
    }
}
impl CaptureTarget {
    pub fn executable(&self) -> Result<String> {
        bounded_string(&self.executable)
    }
    pub fn label(&self) -> Result<String> {
        bounded_string(&self.label)
    }
    pub fn token(&self) -> String {
        format!("{:x}:{:x}", self.process_id, self.creation_time)
    }
}
fn bounded_string(bytes: &[c_char]) -> Result<String> {
    let end = bytes
        .iter()
        .position(|b| *b == 0)
        .ok_or(Error::Contract("Texto de captura sem terminador"))?;
    String::from_utf8(bytes[..end].iter().map(|v| *v as u8).collect())
        .map_err(|_| Error::Contract("Texto de captura não UTF-8"))
}
#[repr(C)]
#[derive(Clone, Copy, Default, Serialize)]
pub struct CaptureState {
    pub status: i32,
    pub error: i32,
    pub process_id: u32,
    pub queued_frames: u32,
    pub captured_frames: u64,
    pub dropped_frames: u64,
    pub underflow_frames: u64,
    pub discontinuities: u64,
    pub packet_age_us: u64,
    pub max_packet_age_us: u64,
    pub target_buffer_frames: u32,
    pub sample_rate: u32,
}
#[repr(C)]
struct CaptureApi {
    refresh: Option<unsafe extern "C" fn(*const clap_plugin) -> i32>,
    target: Option<unsafe extern "C" fn(*const clap_plugin, u32, *mut CaptureTarget) -> bool>,
    configure: Option<unsafe extern "C" fn(*const clap_plugin, u32, *const CaptureTarget) -> bool>,
    snapshot: Option<unsafe extern "C" fn(*const clap_plugin, *mut CaptureState) -> bool>,
}
impl OfflinePlugin {
    fn capture_api(&self) -> Result<&CaptureApi> {
        // SAFETY: versioned trusted C ABI, live owner; extension borrows module.
        unsafe {
            let api = required((*self.plugin).get_extension)?(
                self.plugin,
                c"org.nodivu.capture-source/1".as_ptr(),
            )
            .cast::<CaptureApi>();
            api.as_ref()
                .ok_or(Error::Contract("Plugin sem captura de aplicativos"))
        }
    }
    pub fn capture_snapshot(&self) -> Result<CaptureState> {
        let api = self.capture_api()?;
        let mut state = CaptureState::default();
        // SAFETY: main-only call, correctly sized output borrowed for this call.
        if !unsafe { required(api.snapshot)?(self.plugin, &mut state) } {
            return Err(Error::Contract("Diagnóstico de captura recusado"));
        }
        Ok(state)
    }
    pub fn capture_targets(&self) -> Result<Vec<CaptureTarget>> {
        let api = self.capture_api()?;
        // SAFETY: owner-thread refresh; bounded target array copied by value below.
        let count = unsafe { required(api.refresh)?(self.plugin) };
        if count < 0 {
            return Err(Error::ProcessStatus(count));
        }
        if count > 256 {
            return Err(Error::Contract(
                "Catálogo de aplicativos excede 256 entradas",
            ));
        }
        let mut result = Vec::with_capacity(count as usize);
        for i in 0..count as u32 {
            let mut target = CaptureTarget::default();
            // SAFETY: output POD matches capture-source/1; never retained by plugin.
            if !unsafe { required(api.target)?(self.plugin, i, &mut target) } {
                return Err(Error::Contract("Identidade de aplicativo inválida"));
            }
            target.executable()?;
            target.label()?;
            result.push(target);
        }
        Ok(result)
    }
    pub fn capture_configure(
        &self,
        mode: u32,
        executable: &str,
        process_id: u32,
        creation_time: u64,
    ) -> Result<()> {
        let api = self.capture_api()?;
        if mode > 2 || executable.len() >= 4096 || executable.contains('\0') {
            return Err(Error::Contract("Seleção de captura inválida"));
        }
        let mut target = CaptureTarget {
            process_id,
            creation_time,
            ..CaptureTarget::default()
        };
        for (to, from) in target.executable.iter_mut().zip(executable.bytes()) {
            *to = from as c_char;
        }
        // SAFETY: copied UTF-8/POD on owner thread, no pointer escapes configure.
        if !unsafe { required(api.configure)?(self.plugin, mode, &target) } {
            return Err(Error::Contract(
                "Captura recusou seleção; atualize os aplicativos",
            ));
        }
        Ok(())
    }
}
