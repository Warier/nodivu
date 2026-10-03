//! Adaptador seguro de efeito CLAP com controles independentes do transporte/UI.
//! Preparar fora do loop; process só empresta buffers e consulta atômicos limitados.
use crate::{AudioProcessor, Error, Metadata, ProcessStatus};
use nodivu_block::{MAX_EVENTS, MAX_FRAMES, ParameterEvent, ParameterId, StereoMut};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering::Relaxed};

type Result<T> = std::result::Result<T, Error>;
const BYPASS_RAMP_FRAMES: usize = 480;

#[derive(Clone, Copy, PartialEq)]
struct ControlParameter {
    id: u32,
    min: f64,
    max: f64,
    default: f64,
    readonly: bool,
    stepped: bool,
}
impl ControlParameter {
    fn valid(self, value: f64) -> bool {
        value.is_finite()
            && (self.min..=self.max).contains(&value)
            && (!self.stepped || value.fract() == 0.0)
    }
}

/// Um conjunto por instância. Parâmetros são independentes, coalescidos no próximo bloco.
/// Não representa automação sample-accurate ou uma transação entre vários parâmetros.
pub struct EffectControls {
    plugin_id: String,
    parameters: [Option<ControlParameter>; MAX_EVENTS],
    values: [AtomicU64; MAX_EVENTS],
    bypass: AtomicBool,
}
impl EffectControls {
    /// Copia apenas ID/especificações; não retém metadata nem ponteiros nativos.
    /// Perfil inicial de controles: efeito e até 32 parâmetros (loader aceita até 64).
    pub fn prepare(metadata: &Metadata) -> Result<Self> {
        if metadata.parameters.len() > MAX_EVENTS {
            return Err(Error::Contract(
                "Controle ao vivo exige efeito com até 32 parâmetros",
            ));
        }
        let mut parameters = [None; MAX_EVENTS];
        for (index, info) in metadata.parameters.iter().enumerate() {
            let parameter = ControlParameter {
                id: info.id,
                min: info.min,
                max: info.max,
                default: info.default,
                readonly: info.readonly,
                stepped: info.stepped,
            };
            if info.id == u32::MAX
                || !info.min.is_finite()
                || !info.max.is_finite()
                || !parameter.valid(info.default)
                || parameters[..index]
                    .iter()
                    .flatten()
                    .any(|p: &ControlParameter| p.id == info.id)
            {
                return Err(Error::Contract(
                    "Especificação de parâmetro inválida no controle",
                ));
            }
            parameters[index] = Some(parameter);
        }
        Ok(Self {
            plugin_id: metadata.id.clone(),
            values: std::array::from_fn(|i| {
                AtomicU64::new(parameters[i].map_or(0.0, |p| p.default).to_bits())
            }),
            parameters,
            bypass: AtomicBool::new(false),
        })
    }
    pub fn set_parameter(&self, id: ParameterId, value: f64) -> Result<()> {
        let (index, parameter) = self
            .parameters
            .iter()
            .enumerate()
            .find_map(|(i, p)| p.filter(|p| p.id == id.0).map(|p| (i, p)))
            .ok_or(Error::Contract("Parâmetro desconhecido no controle"))?;
        if parameter.readonly || !parameter.valid(value) {
            return Err(Error::Contract(
                "Valor inválido ou parâmetro somente leitura",
            ));
        }
        self.values[index].store(value.to_bits(), Relaxed);
        Ok(())
    }
    pub fn parameter_value(&self, id: ParameterId) -> Option<f64> {
        self.parameters.iter().enumerate().find_map(|(i, p)| {
            p.filter(|p| p.id == id.0)
                .map(|_| f64::from_bits(self.values[i].load(Relaxed)))
        })
    }
    pub fn set_bypass(&self, bypass: bool) {
        self.bypass.store(bypass, Relaxed);
    }
}

#[derive(Clone, Copy, Default, serde::Serialize)]
pub struct LiveEffectStats {
    pub calls: u64,
    pub dry_copy_frames: u64,
    pub mixed_frames: u64,
    pub parameter_events: u64,
}

/// Não possui plugin/DLL. O token e controles precisam sobreviver a este adaptador.
pub struct LiveEffect<'a, 'plugin> {
    processor: &'a mut AudioProcessor<'plugin>,
    controls: &'a EffectControls,
    dry: [[f32; MAX_FRAMES]; 2],
    last: [Option<u64>; MAX_EVENTS],
    wet: f32,
    target: f32,
    step: f32,
    remaining: usize,
    stats: LiveEffectStats,
}
impl<'a, 'plugin> LiveEffect<'a, 'plugin> {
    /// Usar após activate/start, antes do loop. Bypass sem compensação exige latência zero.
    pub fn prepare(
        processor: &'a mut AudioProcessor<'plugin>,
        controls: &'a EffectControls,
    ) -> Result<Self> {
        let metadata = processor.metadata();
        if metadata.latency_frames != 0 || metadata.id != controls.plugin_id {
            return Err(Error::Contract(
                "Efeito/controles incompatíveis ou latência sem compensação",
            ));
        }
        // Controles congelados precisam corresponder à especificação da instância ativada.
        if metadata.parameters.len() != controls.parameters.iter().flatten().count()
            || metadata
                .parameters
                .iter()
                .zip(&controls.parameters)
                .any(|(p, c)| {
                    *c != Some(ControlParameter {
                        id: p.id,
                        min: p.min,
                        max: p.max,
                        default: p.default,
                        readonly: p.readonly,
                        stepped: p.stepped,
                    })
                })
        {
            return Err(Error::Contract(
                "Especificação mudou após preparar controles",
            ));
        }
        let wet = if controls.bypass.load(Relaxed) {
            0.0
        } else {
            1.0
        };
        Ok(Self {
            processor,
            controls,
            dry: [[0.0; MAX_FRAMES]; 2],
            last: [None; MAX_EVENTS],
            wet,
            target: wet,
            step: 0.0,
            remaining: 0,
            stats: LiveEffectStats::default(),
        })
    }
    pub fn stats(&self) -> LiveEffectStats {
        self.stats
    }
    pub fn reset(&mut self) -> Result<()> {
        self.processor.reset()?;
        self.last = [None; MAX_EVENTS];
        self.wet = if self.controls.bypass.load(Relaxed) {
            0.0
        } else {
            1.0
        };
        self.target = self.wet;
        self.remaining = 0;
        self.stats = LiveEffectStats::default();
        Ok(())
    }
    pub fn cancelled(&self) -> bool {
        self.processor.cancelled()
    }
    pub fn process(&mut self, block: StereoMut<'_>) -> Result<ProcessStatus> {
        let frames = block.left.len();
        if frames == 0 || frames > MAX_FRAMES || frames != block.right.len() {
            return Err(Error::Contract("Buffer inválido no efeito ao vivo"));
        }
        if !self.processor.metadata().has_output && self.controls.bypass.load(Relaxed) {
            return Ok(ProcessStatus::Sleep);
        }
        let target = if self.controls.bypass.load(Relaxed) {
            0.0
        } else {
            1.0
        };
        if target != self.target {
            self.target = target;
            self.remaining = BYPASS_RAMP_FRAMES;
            self.step = (target - self.wet) / BYPASS_RAMP_FRAMES as f32;
        }
        let separate = !self.processor.metadata().supports_in_place;
        let mix = self.remaining > 0 || self.wet < 1.0;
        if separate || mix {
            if self.processor.metadata().has_input {
                self.dry[0][..frames].copy_from_slice(block.left);
                self.dry[1][..frames].copy_from_slice(block.right);
            } else {
                self.dry[0][..frames].fill(0.0);
                self.dry[1][..frames].fill(0.0);
            }
            self.stats.dry_copy_frames = self.stats.dry_copy_frames.saturating_add(frames as u64);
        }
        let mut events = [ParameterEvent {
            id: ParameterId(0),
            frame_offset: 0,
            value: 0.0,
        }; MAX_EVENTS];
        let mut values = self.last;
        let mut count = 0;
        for (index, parameter) in self.controls.parameters.iter().enumerate() {
            if let Some(parameter) = parameter
                && !parameter.readonly
            {
                let value = self.controls.values[index].load(Relaxed);
                if self.last[index] != Some(value) {
                    events[count] = ParameterEvent {
                        id: ParameterId(parameter.id),
                        frame_offset: 0,
                        value: f64::from_bits(value),
                    };
                    values[index] = Some(value);
                    count += 1;
                }
            }
        }
        // Continua processando em bypass: preserva estado e atende eventos sem reopen/reset.
        let status = if separate {
            self.processor.process(
                self.processor
                    .metadata()
                    .has_input
                    .then_some((&self.dry[0][..frames], &self.dry[1][..frames])),
                StereoMut::new(block.left, block.right),
                &events[..count],
            )
        } else {
            self.processor
                .process_in_place(StereoMut::new(block.left, block.right), &events[..count])
        }?;
        self.last = values;
        self.stats.calls = self.stats.calls.saturating_add(1);
        self.stats.parameter_events = self.stats.parameter_events.saturating_add(count as u64);
        if mix {
            for i in 0..frames {
                if self.remaining > 0 {
                    self.remaining -= 1;
                    self.wet = if self.remaining == 0 {
                        self.target
                    } else {
                        (self.wet + self.step).clamp(0.0, 1.0)
                    };
                }
                block.left[i] = (1.0 - self.wet) * self.dry[0][i] + self.wet * block.left[i];
                block.right[i] = (1.0 - self.wet) * self.dry[1][i] + self.wet * block.right[i];
            }
            self.stats.mixed_frames = self.stats.mixed_frames.saturating_add(frames as u64);
        }
        Ok(status)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::ParameterInfo;
    fn metadata() -> Metadata {
        Metadata {
            id: "test.controls".into(),
            name: "Descritor sintético para teste de controles".into(),
            has_input: true,
            has_output: true,
            supports_in_place: true,
            latency_frames: 0,
            parameters: vec![
                ParameterInfo {
                    id: 7,
                    name: "Ganho".into(),
                    min: 0.0,
                    max: 1.0,
                    default: 1.0,
                    readonly: false,
                    stepped: false,
                },
                ParameterInfo {
                    id: 8,
                    name: "Leitura".into(),
                    min: 0.0,
                    max: 1.0,
                    default: 0.5,
                    readonly: true,
                    stepped: false,
                },
                ParameterInfo {
                    id: 9,
                    name: "Passo".into(),
                    min: -2.0,
                    max: 2.0,
                    default: 0.0,
                    readonly: false,
                    stepped: true,
                },
            ],
        }
    }
    #[test]
    fn rejected_values_preserve_controls_and_instances_remain_independent() {
        let metadata = metadata();
        let first = EffectControls::prepare(&metadata).unwrap();
        let other = EffectControls::prepare(&metadata).unwrap();
        first.set_parameter(ParameterId(7), 0.25).unwrap();
        for value in [f64::NAN, f64::INFINITY, -0.1, 1.1] {
            assert!(first.set_parameter(ParameterId(7), value).is_err());
        }
        assert!(first.set_parameter(ParameterId(8), 0.0).is_err());
        assert!(first.set_parameter(ParameterId(9), 0.5).is_err());
        assert!(first.set_parameter(ParameterId(999), 0.5).is_err());
        first.set_parameter(ParameterId(9), -1.0).unwrap();
        assert_eq!(first.parameter_value(ParameterId(7)), Some(0.25));
        assert_eq!(first.parameter_value(ParameterId(8)), Some(0.5));
        assert_eq!(first.parameter_value(ParameterId(9)), Some(-1.0));
        assert_eq!(other.parameter_value(ParameterId(7)), Some(1.0));
        first.set_bypass(true);
        assert!(!other.bypass.load(Relaxed));
    }
    #[test]
    fn rejects_unsupported_profile_and_invalid_parameter_schema_before_audio() {
        let mut spec = metadata();
        spec.has_input = false;
        assert!(EffectControls::prepare(&spec).is_ok());
        spec.has_input = true;
        spec.parameters[2].id = 7;
        assert!(EffectControls::prepare(&spec).is_err());
        spec.parameters[2].id = 9;
        spec.parameters[2].default = 0.5;
        assert!(EffectControls::prepare(&spec).is_err());
        spec.parameters[2].default = 0.0;
        for id in 10..40 {
            spec.parameters.push(ParameterInfo {
                id,
                name: "Extra".into(),
                min: 0.0,
                max: 1.0,
                default: 0.0,
                readonly: false,
                stepped: false,
            });
        }
        assert!(EffectControls::prepare(&spec).is_err());
        spec.parameters.pop();
        assert!(EffectControls::prepare(&spec).is_ok());
    }
}
