//! Fonte de teste independente: saída planar, estado local, nenhuma dependência do motor.
#![forbid(unsafe_code)]
use nodivu_block::{
    Descriptor, Parameter, ParameterEvent, ParameterId, PortId, PreparedSource, ProcessError,
    ProcessSpec, StereoMut, StereoPort, validate_events,
};
pub const FREQUENCY_HZ: ParameterId = ParameterId(0);
pub static DESCRIPTOR: Descriptor = Descriptor {
    id: "org.nodivu.tone",
    name: "Tom de teste",
    input: None,
    output: StereoPort {
        id: PortId(0),
        name: "Áudio",
    },
    parameters: &[Parameter {
        id: FREQUENCY_HZ,
        name: "Frequência (Hz)",
        min: 20.0,
        max: 20_000.0,
        default: 440.0,
    }],
    latency_frames: 0,
    tail_frames: 0,
};
pub struct Tone {
    spec: ProcessSpec,
    phase: f32,
    frequency: f32,
}
impl Default for Tone {
    fn default() -> Self {
        Self::prepare(ProcessSpec::STEREO_48K)
    }
}
impl Tone {
    pub fn prepare(spec: ProcessSpec) -> Self {
        Self {
            spec,
            phase: 0.0,
            frequency: 440.0,
        }
    }
}
impl PreparedSource for Tone {
    fn descriptor(&self) -> &'static Descriptor {
        &DESCRIPTOR
    }
    fn reset(&mut self) {
        self.phase = 0.0;
    }
    fn generate(
        &mut self,
        output: StereoMut<'_>,
        events: &[ParameterEvent],
    ) -> Result<(), ProcessError> {
        let frames = output.validate(self.spec)?;
        validate_events(events, frames, &DESCRIPTOR)?;
        let mut event = 0;
        for (i, (left, right)) in output
            .left
            .iter_mut()
            .zip(output.right.iter_mut())
            .enumerate()
        {
            while event < events.len() && events[event].frame_offset as usize == i {
                self.frequency = events[event].value as f32;
                event += 1;
            }
            *left = self.phase.sin() * 0.06309573;
            *right = *left; // -24 dBFS; monitoramento fica no host.
            self.phase = (self.phase
                + std::f32::consts::TAU * self.frequency / self.spec.sample_rate_hz() as f32)
                % std::f32::consts::TAU;
        }
        Ok(())
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn segmentation_does_not_reset_phase_and_overwrites_output() {
        let mut whole = Tone::default();
        let mut split = Tone::default();
        let mut l = [f32::NAN; 480];
        let mut r = [f32::NAN; 480];
        whole.generate(StereoMut::new(&mut l, &mut r), &[]).unwrap();
        let mut a = [0.0; 480];
        let mut b = [0.0; 480];
        for offset in (0..480).step_by(64) {
            let end = (offset + 64).min(480);
            split
                .generate(
                    StereoMut::new(&mut a[offset..end], &mut b[offset..end]),
                    &[],
                )
                .unwrap();
        }
        assert_eq!(l, a);
        assert_eq!(r, b);
        assert!(l.iter().all(|s| s.is_finite() && s.abs() <= 0.063096));
    }
    #[test]
    fn invalid_event_does_not_advance_source_and_reset_preserves_frequency() {
        let mut source = Tone::default();
        let mut reference = Tone::default();
        let mut l = [1.0; 128];
        let mut r = [1.0; 128];
        assert!(
            source
                .generate(
                    StereoMut::new(&mut l, &mut r),
                    &[ParameterEvent {
                        id: FREQUENCY_HZ,
                        frame_offset: 0,
                        value: f64::INFINITY
                    }]
                )
                .is_err()
        );
        assert_eq!(l, [1.0; 128]);
        source
            .generate(StereoMut::new(&mut l, &mut r), &[])
            .unwrap();
        let mut a = [0.0; 128];
        let mut b = [0.0; 128];
        reference
            .generate(StereoMut::new(&mut a, &mut b), &[])
            .unwrap();
        assert_eq!(a, l);
        source
            .generate(
                StereoMut::new(&mut l, &mut r),
                &[ParameterEvent {
                    id: FREQUENCY_HZ,
                    frame_offset: 64,
                    value: 1000.0,
                }],
            )
            .unwrap();
        source.reset();
        assert_eq!(source.frequency, 1000.0);
        assert_eq!(source.phase, 0.0);
    }
}
