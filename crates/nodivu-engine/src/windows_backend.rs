//! Única fronteira FFI: COM e memória nativa não escapam destas funções.
use crate::BackendError;
use nodivu_core::*;
use std::marker::PhantomData;
use std::rc::Rc;
use uuid::Uuid;
use windows::{
    Win32::{
        Devices::FunctionDiscovery::PKEY_Device_FriendlyName,
        Foundation::ERROR_NOT_FOUND,
        Media::Audio::*,
        System::Com::{
            StructuredStorage::{PROPVARIANT, PropVariantClear, PropVariantToStringAlloc},
            *,
        },
    },
    core::{HRESULT, PWSTR},
};

// !Send/!Sync: a finalização deve ocorrer na thread que inicializou COM.
pub(super) struct Apartment(PhantomData<Rc<()>>);
impl Apartment {
    pub(super) fn new() -> windows::core::Result<Self> {
        // SAFETY: thread do chamador; nenhum ponteiro reservado. S_OK e S_FALSE
        // exigem uma chamada a CoUninitialize; falhas não criam o guard.
        unsafe {
            CoInitializeEx(None, COINIT_MULTITHREADED).ok()?;
        }
        Ok(Self(PhantomData))
    }
}
impl Drop for Apartment {
    fn drop(&mut self) {
        // SAFETY: guard não cruza threads e vive mais que todos os objetos COM.
        unsafe { CoUninitialize() }
    }
}
struct TaskString(PWSTR);
struct PropertyValue(PROPVARIANT);
impl Drop for PropertyValue {
    fn drop(&mut self) {
        // SAFETY: valor inicializado por GetValue, propriedade somente leitura,
        // possui o conteúdo e é limpo exatamente uma vez, inclusive em erro.
        let _ = unsafe { PropVariantClear(&mut self.0) };
    }
}
impl TaskString {
    fn string(&self) -> windows::core::Result<String> {
        // SAFETY: APIs produtoras retornam string UTF-16 terminada por NUL;
        // o guard possui a alocação até terminar a conversão, inclusive em erro.
        unsafe { Ok(self.0.to_string()?) }
    }
}
impl Drop for TaskString {
    fn drop(&mut self) {
        // SAFETY: GetId/PropVariantToStringAlloc usam o alocador COM.
        unsafe { CoTaskMemFree(Some(self.0.0.cast())) }
    }
}
fn endpoint_id(device: &IMMDevice) -> windows::core::Result<DeviceId> {
    // SAFETY: interface válida, no apartamento que a criou; resultado tem ownership.
    let id = TaskString(unsafe { device.GetId()? });
    Ok(DeviceId(id.string()?))
}

pub fn new_id() -> Result<Uuid, BackendError> {
    // SAFETY: CoCreateGuid não requer apartamento; binding fornece out-pointer válido.
    let guid = unsafe { CoCreateGuid() }.map_err(|e| BackendError::new("Gerar UUID", e))?;
    Ok(Uuid::from_u128(guid.to_u128()))
}

pub fn enumerate() -> Result<Vec<DeviceInfo>, BackendError> {
    inventory().map(|inventory| inventory.devices)
}

fn failure(operation: &'static str, error: windows::core::Error) -> BackendError {
    BackendError::new(operation, error)
}

pub fn inventory() -> Result<crate::discovery::DeviceInventory, BackendError> {
    let _apartment = Apartment::new().map_err(|e| failure("Inicializar COM para áudio", e))?;
    // SAFETY: local COM apartment owns all interfaces until this call returns.
    let enumerator: IMMDeviceEnumerator =
        unsafe { CoCreateInstance(&MMDeviceEnumerator, None, CLSCTX_ALL) }
            .map_err(|e| failure("Criar enumerador MMDevice", e))?;
    crate::discovery::DeviceInventory::merge_flows([
        enumerate_flow(&enumerator, eCapture, Flow::Capture),
        enumerate_flow(&enumerator, eRender, Flow::Render),
    ])
}

fn enumerate_flow(
    enumerator: &IMMDeviceEnumerator,
    native_flow: EDataFlow,
    flow: Flow,
) -> Result<crate::discovery::DeviceInventory, BackendError> {
    let label = if flow == Flow::Capture {
        "entrada"
    } else {
        "saída"
    };
    let operation = if flow == Flow::Capture {
        "Enumerar entradas ativas MMDevice"
    } else {
        "Enumerar saídas ativas MMDevice"
    };
    let mut result = crate::discovery::DeviceInventory::default();
    // SAFETY: only usable endpoints are requested; old/disconnected driver records
    // need not have a readable property store and must not block active devices.
    let collection = unsafe { enumerator.EnumAudioEndpoints(native_flow, DEVICE_STATE_ACTIVE) }
        .map_err(|e| failure(operation, e))?;
    // SAFETY: the local collection remains alive for the full iteration.
    let count = unsafe { collection.GetCount() }.map_err(|e| failure(operation, e))?;
    // A broken default is a hint failure, never a reason to discard the collection.
    // SAFETY: same apartment, documented direction and role constants.
    let default = match unsafe { enumerator.GetDefaultAudioEndpoint(native_flow, eConsole) }
        .and_then(|d| endpoint_id(&d))
    {
        Ok(id) => Some(id),
        Err(e) if e.code() == HRESULT::from_win32(ERROR_NOT_FOUND.0) => None,
        Err(e) => {
            result
                .warnings
                .push(format!("Consultar padrão de {label}: {e}"));
            None
        }
    };
    for i in 0..count {
        let device = read_device(&collection, i, flow, default.as_ref(), &mut result.warnings);
        match device {
            Ok(Some(device)) => result.devices.push(device),
            Ok(None) => {} // Removed/disabled between enumeration and GetState.
            Err(e) => result.warnings.push(format!("{label}, item {i}: {e}")),
        }
    }
    Ok(result)
}

fn friendly_name(device: &IMMDevice) -> windows::core::Result<String> {
    // SAFETY: local valid interface, read-only store, RAII owns native values.
    let properties = unsafe { device.OpenPropertyStore(STGM_READ)? };
    // SAFETY: official key, initialized value is freed by PropertyValue.
    let value = PropertyValue(unsafe { properties.GetValue(&PKEY_Device_FriendlyName)? });
    // SAFETY: valid property, allocated string is freed by TaskString.
    TaskString(unsafe { PropVariantToStringAlloc(&value.0)? }).string()
}

fn read_device(
    collection: &IMMDeviceCollection,
    index: u32,
    flow: Flow,
    default: Option<&DeviceId>,
    warnings: &mut Vec<String>,
) -> Result<Option<DeviceInfo>, BackendError> {
    // SAFETY: index is bounded by GetCount; concurrent removal is a recoverable error.
    let device = unsafe { collection.Item(index) }.map_err(|e| failure("Obter endpoint", e))?;
    let id = endpoint_id(&device).map_err(|e| failure("Ler ID do endpoint", e))?;
    // SAFETY: local interface, read-only query, no audio stream opened.
    let state =
        unsafe { device.GetState() }.map_err(|e| failure("Consultar estado do endpoint", e))?;
    if state != DEVICE_STATE_ACTIVE {
        return Ok(None);
    }
    let name = match friendly_name(&device) {
        Ok(name) if !name.trim().is_empty() => name,
        value => {
            let reason = value
                .err()
                .map_or_else(|| "nome vazio".to_string(), |e| e.to_string());
            warnings.push(format!("Ler nome do endpoint {}: {reason}", id.0));
            // This is a real active endpoint, identified by its actual opaque ID.
            format!("Dispositivo sem nome ({})", id.0)
        }
    };
    Ok(Some(DeviceInfo {
        is_default: default == Some(&id),
        endpoint_id: id,
        name,
        flow,
        state: DeviceState::Active,
        hardware_kind: HardwareKind::Unknown,
        support: Support::Unknown,
        reason: Some(
            "Origem física/virtual desconhecida; o formato será verificado ao iniciar.".into(),
        ),
    }))
}
