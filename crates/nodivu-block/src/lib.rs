//! Contrato Rust seguro do processamento, alinhado ao perfil CLAP planejado.
//! Não é ABI de DLL: o futuro adaptador CLAP traduzirá lifecycle/eventos para esta borda.
//! Preparação/destruição fora do áudio; process/reset sem heap, I/O, espera ou locks.
#![forbid(unsafe_code)]

pub const MAX_FRAMES: usize = 480;
pub const MAX_EVENTS: usize = 32;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ParameterId(pub u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct PortId(pub u32);

/// Perfil inicial: cada porta transporta exatamente dois canais float32 planares.
#[derive(Clone, Copy, Debug)]
pub struct StereoPort {
    pub id: PortId,
    pub name: &'static str,
}

#[derive(Clone, Copy, Debug)]
pub struct Parameter {
    pub id: ParameterId,
    pub name: &'static str,
    pub min: f64,
    pub max: f64,
    pub default: f64,
}

#[derive(Clone, Copy, Debug)]
pub struct Descriptor {
    pub id: &'static str,
    pub name: &'static str,
    pub parameters: &'static [Parameter],
    pub input: Option<StereoPort>,
    pub output: StereoPort,
    pub latency_frames: u32,
    pub tail_frames: u32,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProcessError {
    InvalidSpec,
    InvalidBuffer,
    TooManyEvents,
    InvalidEvent,
    UnknownParameter,
}
impl std::fmt::Display for ProcessError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(match self {
            Self::InvalidSpec => "Configuração de processamento não suportada",
            Self::InvalidBuffer => "Buffer estéreo vazio, desigual ou acima do limite",
            Self::TooManyEvents => "Quantidade de eventos excede o limite do bloco",
            Self::InvalidEvent => "Parâmetro, ordem ou instante de evento inválido",
            Self::UnknownParameter => "Parâmetro desconhecido pelo bloco",
        })
    }
}
impl std::error::Error for ProcessError {}

#[derive(Clone, Copy, Debug)]
pub struct ProcessSpec {
    sample_rate_hz: u32,
    max_frames: usize,
}
impl ProcessSpec {
    pub const STEREO_48K: Self = Self {
        sample_rate_hz: 48_000,
        max_frames: MAX_FRAMES,
    };
    pub fn new(sample_rate_hz: u32, max_frames: usize) -> Result<Self, ProcessError> {
        if sample_rate_hz != 48_000 || !(1..=MAX_FRAMES).contains(&max_frames) {
            return Err(ProcessError::InvalidSpec);
        }
        Ok(Self {
            sample_rate_hz,
            max_frames,
        })
    }
    pub fn sample_rate_hz(self) -> u32 {
        self.sample_rate_hz
    }
    pub fn max_frames(self) -> usize {
        self.max_frames
    }
}

/// Canais disjuntos emprestados somente durante process; não se retêm ponteiros.
pub struct StereoMut<'a> {
    pub left: &'a mut [f32],
    pub right: &'a mut [f32],
}
impl<'a> StereoMut<'a> {
    pub fn new(left: &'a mut [f32], right: &'a mut [f32]) -> Self {
        Self { left, right }
    }
    pub fn validate(&self, spec: ProcessSpec) -> Result<usize, ProcessError> {
        if self.left.is_empty()
            || self.left.len() != self.right.len()
            || self.left.len() > spec.max_frames
        {
            return Err(ProcessError::InvalidBuffer);
        }
        Ok(self.left.len())
    }
}

#[derive(Clone, Copy, Debug)]
pub struct ParameterEvent {
    pub frame_offset: u32,
    pub id: ParameterId,
    pub value: f64,
}

/// Validar o lote inteiro antes de modificar estado/áudio. Sem alocação no erro.
pub fn validate_events(
    events: &[ParameterEvent],
    frames: usize,
    descriptor: &Descriptor,
) -> Result<(), ProcessError> {
    if events.len() > MAX_EVENTS {
        return Err(ProcessError::TooManyEvents);
    }
    let mut previous = 0;
    for event in events {
        if event.frame_offset < previous || event.frame_offset as usize >= frames {
            return Err(ProcessError::InvalidEvent);
        }
        let parameter = descriptor
            .parameters
            .iter()
            .find(|p| p.id == event.id)
            .ok_or(ProcessError::UnknownParameter)?;
        if !event.value.is_finite() || !(parameter.min..=parameter.max).contains(&event.value) {
            return Err(ProcessError::InvalidEvent);
        }
        previous = event.frame_offset;
    }
    Ok(())
}

/// Efeito estéreo in-place preparado. Uma única chamada ativa por instância.
/// Erros devem preservar parâmetros/áudio; host descarta/silencia o bloco com erro.
/// Sem padrão de bypass automático: cada algoritmo declara latência/cauda.
pub trait PreparedEffect {
    fn descriptor(&self) -> &'static Descriptor;
    fn reset(&mut self);
    fn process(
        &mut self,
        audio: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<(), ProcessError>;
}

/// Fonte preparada sem entrada. Escreve todos os frames; nunca faz leitura de arquivo aqui.
/// Um futuro player usa leitura/decodificação antecipada fora deste método.
pub trait PreparedSource {
    fn descriptor(&self) -> &'static Descriptor;
    fn reset(&mut self);
    fn generate(
        &mut self,
        output: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<(), ProcessError>;
}
