//! Exemplo de efeito independente do motor/UI. Vinculado em código nesta fatia, não DLL CLAP.
#![forbid(unsafe_code)]
use nodivu_block::{
    Descriptor, Parameter, ParameterEvent, ParameterId, PreparedEffect, ProcessError, ProcessSpec,
    StereoMut, validate_events,
};

pub const AMPLITUDE: ParameterId = ParameterId(0);
pub static DESCRIPTOR: Descriptor = Descriptor {
    input: Some(nodivu_block::StereoPort {
        id: nodivu_block::PortId(0),
        name: "Entrada",
    }),
    output: nodivu_block::StereoPort {
        id: nodivu_block::PortId(0),
        name: "Saída",
    },
    id: "org.nodivu.gain",
    name: "Ganho",
    parameters: &[Parameter {
        id: AMPLITUDE,
        name: "Amplitude linear",
        min: 0.0,
        max: 16.0,
        default: 1.0,
    }],
    latency_frames: 0,
    tail_frames: 0,
};

pub struct Gain {
    spec: ProcessSpec,
    value: f32,
    target: f32,
    step: f32,
    remaining: u32,
}
impl Default for Gain {
    fn default() -> Self {
        Self {
            spec: ProcessSpec::STEREO_48K,
            value: 0.0,
            target: 0.0,
            step: 0.0,
            remaining: 0,
        }
    }
}
impl Gain {
    pub fn unity() -> Self {
        Self {
            value: 1.0,
            target: 1.0,
            ..Self::default()
        }
    }
    /// A rampa suaviza o parâmetro, sem reter frames de áudio.
    pub fn prepare(spec: ProcessSpec, initial_amplitude: f32) -> Result<Self, ProcessError> {
        if !initial_amplitude.is_finite() || !(0.0..=16.0).contains(&initial_amplitude) {
            return Err(ProcessError::InvalidEvent);
        }
        Ok(Self {
            spec,
            value: initial_amplitude,
            target: initial_amplitude,
            step: 0.0,
            remaining: 0,
        })
    }
    fn target(&mut self, target: f32) {
        if target != self.target {
            self.target = target;
            self.remaining = self.spec.sample_rate_hz() / 100;
            self.step = (target - self.value) / self.remaining as f32;
        }
    }
}
impl PreparedEffect for Gain {
    fn descriptor(&self) -> &'static Descriptor {
        &DESCRIPTOR
    }
    fn reset(&mut self) {
        // Reset preserva o parâmetro, reiniciando apenas a transição.
        self.value = 0.0;
        self.remaining = self.spec.sample_rate_hz() / 100;
        self.step = self.target / self.remaining as f32;
    }
    fn process(
        &mut self,
        audio: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<(), ProcessError> {
        let frames = audio.validate(self.spec)?;
        validate_events(events, frames, &DESCRIPTOR)?;
        let mut event_index = 0;
        for (frame, (left, right)) in audio
            .left
            .iter_mut()
            .zip(audio.right.iter_mut())
            .enumerate()
        {
            while event_index < events.len() && events[event_index].frame_offset as usize == frame {
                self.target(events[event_index].value as f32);
                event_index += 1;
            }
            if self.remaining > 0 {
                self.value += self.step;
                self.remaining -= 1;
                if self.remaining == 0 {
                    self.value = self.target;
                }
            }
            *left *= self.value;
            *right *= self.value;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn prepared(value: f32) -> Gain {
        Gain::prepare(ProcessSpec::new(48_000, 480).unwrap(), value).unwrap()
    }
    #[test]
    fn rejects_entire_invalid_event_batch_without_mutation() {
        let mut gain = prepared(1.0);
        let mut l = [1.0; 64];
        let mut r = [0.5; 64];
        let events = [
            ParameterEvent {
                frame_offset: 0,
                id: AMPLITUDE,
                value: 0.0,
            },
            ParameterEvent {
                frame_offset: 1,
                id: AMPLITUDE,
                value: f64::NAN,
            },
        ];
        assert!(
            gain.process(StereoMut::new(&mut l, &mut r), &events)
                .is_err()
        );
        assert_eq!(l, [1.0; 64]);
        gain.process(StereoMut::new(&mut l, &mut r), &[]).unwrap();
        assert_eq!(l, [1.0; 64]);
        assert_eq!(r, [0.5; 64]);
    }
    #[test]
    fn offsets_variable_blocks_reset_and_instances() {
        let mut a = prepared(1.0);
        let mut b = prepared(0.5);
        let mut l = [1.0; 480];
        let mut r = [1.0; 480];
        a.process(
            StereoMut::new(&mut l[..64], &mut r[..64]),
            &[ParameterEvent {
                frame_offset: 32,
                id: AMPLITUDE,
                value: 0.0,
            }],
        )
        .unwrap();
        assert_eq!(l[..32], [1.0; 32]);
        assert!(l[32] < 1.0);
        a.process(StereoMut::new(&mut l[..448], &mut r[..448]), &[])
            .unwrap();
        assert_eq!(l[447], 0.0);
        b.process(StereoMut::new(&mut l[448..], &mut r[448..]), &[])
            .unwrap();
        assert_eq!(l[479], 0.5);
        a.reset();
        l.fill(1.0);
        r.fill(1.0);
        a.process(StereoMut::new(&mut l, &mut r), &[]).unwrap();
        assert_eq!(l, [0.0; 480]);
    }
    #[test]
    fn chain_has_no_added_frames_and_rejects_bad_layouts() {
        let mut chain: Vec<Box<dyn PreparedEffect>> = (0..8)
            .map(|_| Box::new(prepared(0.5)) as Box<dyn PreparedEffect>)
            .collect();
        let mut l = [0.0; 128];
        let mut r = [0.0; 128];
        l[17] = 1.0;
        r[17] = -1.0;
        for effect in &mut chain {
            effect.process(StereoMut::new(&mut l, &mut r), &[]).unwrap();
        }
        assert_eq!(l[17], 1.0 / 256.0);
        assert_eq!(r[17], -1.0 / 256.0);
        assert_eq!(l.iter().filter(|v| **v != 0.0).count(), 1);
        assert!(
            chain[0]
                .process(StereoMut::new(&mut l[..64], &mut r[..63]), &[])
                .is_err()
        );
        let bad = [ParameterEvent {
            frame_offset: 128,
            id: AMPLITUDE,
            value: 1.0,
        }];
        assert!(
            chain[0]
                .process(StereoMut::new(&mut l, &mut r), &bad)
                .is_err()
        );
        let too_many = [ParameterEvent {
            frame_offset: 0,
            id: AMPLITUDE,
            value: 1.0,
        }; 33];
        assert!(
            chain[0]
                .process(StereoMut::new(&mut l, &mut r), &too_many)
                .is_err()
        );
    }
}
