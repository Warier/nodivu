//! Offline/embedded validation executor for the v2 contract. Not the WASAPI executor yet.
//! Prepare and drop outside processing; node callbacks must obey the same RT rules.
use nodivu_core::{
    ApiError,
    graph::NodeId,
    routing::{Compiled, Descriptor, MAX_PORTS, Patch, Role},
};
pub const FRAMES: usize = 480;
pub type Stereo = [[f32; FRAMES]; 2];
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ProcessError {
    InvalidFrames,
    NodeFailed(NodeId),
    NonFinite(NodeId),
}
struct Delay {
    samples: Vec<[f32; 2]>,
    cursor: usize,
}
impl Delay {
    fn apply(&mut self, source: &Stereo, target: &mut Stereo, frames: usize) {
        if self.samples.is_empty() {
            for (to, from) in target.iter_mut().zip(source) {
                to[..frames].copy_from_slice(&from[..frames]);
            }
            return;
        }
        let [left, right] = target;
        for ((l, r), (input_l, input_r)) in left
            .iter_mut()
            .zip(right)
            .zip(source[0].iter().zip(&source[1]))
            .take(frames)
        {
            [*l, *r] = self.samples[self.cursor];
            self.samples[self.cursor] = [*input_l, *input_r];
            self.cursor = (self.cursor + 1) % self.samples.len();
        }
    }
}
pub struct Prepared {
    plan: Compiled,
    buffers: Vec<Stereo>,
    inputs: Vec<Stereo>,
    outputs: Vec<Stereo>,
    delays: Vec<Vec<Delay>>,
}
impl Prepared {
    pub fn new(patch: &Patch, registry: &[Descriptor], roots: &[NodeId]) -> Result<Self, ApiError> {
        let plan = patch.compile(registry, roots)?;
        let buffers = vec![[[0.0; FRAMES]; 2]; plan.buffer_count];
        let delays = plan
            .steps
            .iter()
            .map(|step| {
                step.inputs
                    .iter()
                    .map(|input| Delay {
                        samples: vec![[0.0; 2]; input.delay_frames as usize],
                        cursor: 0,
                    })
                    .collect()
            })
            .collect();
        Ok(Self {
            plan,
            buffers,
            inputs: vec![[[0.0; FRAMES]; 2]; MAX_PORTS],
            outputs: vec![[[0.0; FRAMES]; 2]; MAX_PORTS],
            delays,
        })
    }
    pub fn plan(&self) -> &Compiled {
        &self.plan
    }
    /// Called once per quantum. Outputs are published once and only borrowed/copied by branches.
    /// Inputs/outputs are sorted by stable port ID; unconnected inputs contain silence.
    /// Mixer sums, without clipping/normalization. Gain is explicit in preceding effect nodes.
    /// Only the first `frames` samples of each channel are valid. A failure aborts the
    /// remaining steps, but cannot undo consumers already called in other branches.
    /// The caller owns fault recovery and resetting DSP state; this is not a transaction.
    pub fn process(
        &mut self,
        frames: usize,
        mut node: impl FnMut(NodeId, Role, &[Stereo], &mut [Stereo], usize) -> bool,
    ) -> Result<(), ProcessError> {
        if !(1..=FRAMES).contains(&frames) {
            return Err(ProcessError::InvalidFrames);
        }
        for (index, step) in self.plan.steps.iter().enumerate() {
            for (i, binding) in step.inputs.iter().enumerate() {
                if let Some(buffer) = binding.buffer {
                    self.delays[index][i].apply(&self.buffers[buffer], &mut self.inputs[i], frames);
                } else {
                    for channel in &mut self.inputs[i] {
                        channel[..frames].fill(0.0);
                    }
                }
            }
            for output in &mut self.outputs[..step.outputs.len()] {
                for channel in output {
                    channel[..frames].fill(0.0);
                }
            }
            if step.role == Role::Mixer {
                for input in &self.inputs[..step.inputs.len()] {
                    for (to, from) in self.outputs[0].iter_mut().zip(input) {
                        for (sample, value) in to[..frames].iter_mut().zip(&from[..frames]) {
                            *sample += value;
                        }
                    }
                }
            } else if !node(
                step.id,
                step.role,
                &self.inputs[..step.inputs.len()],
                &mut self.outputs[..step.outputs.len()],
                frames,
            ) {
                return Err(ProcessError::NodeFailed(step.id));
            }
            if self.outputs[..step.outputs.len()].iter().any(|output| {
                output
                    .iter()
                    .any(|c| c[..frames].iter().any(|v| !v.is_finite()))
            }) {
                return Err(ProcessError::NonFinite(step.id));
            }
            for (i, binding) in step.outputs.iter().enumerate() {
                for (to, from) in self.buffers[binding.buffer]
                    .iter_mut()
                    .zip(&self.outputs[i])
                {
                    to[..frames].copy_from_slice(&from[..frames]);
                }
            }
        }
        Ok(())
    }
    /// Clear compensation history on route replacement; resetting plugin state is owner's duty.
    pub fn reset(&mut self) {
        for line in self.delays.iter_mut().flatten() {
            line.samples.fill([0.0; 2]);
            line.cursor = 0;
        }
        self.buffers.fill([[0.0; FRAMES]; 2]);
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nodivu_core::routing::{Connection, Endpoint, Instance, PortId};
    fn id(n: u128) -> NodeId {
        NodeId(uuid::Uuid::from_u128(n))
    }
    fn fixture(latency: u32) -> Prepared {
        let descriptor =
            |name: &str, role, inputs: Vec<PortId>, outputs: Vec<PortId>, latency_frames| {
                Descriptor {
                    plugin_id: name.into(),
                    role,
                    inputs,
                    outputs,
                    latency_frames,
                }
            };
        let registry = [
            descriptor("s", Role::Source, vec![], vec![PortId(0)], 0),
            descriptor("e", Role::Effect, vec![PortId(0)], vec![PortId(0)], latency),
            descriptor(
                "m",
                Role::Mixer,
                vec![PortId(0), PortId(1)],
                vec![PortId(0)],
                0,
            ),
            descriptor("c", Role::Consumer, vec![PortId(0)], vec![], 0),
        ];
        let patch = Patch {
            schema_version: 2,
            nodes: [(1, "s"), (2, "e"), (3, "m"), (4, "c"), (5, "c")]
                .into_iter()
                .map(|(i, p)| Instance {
                    id: id(i),
                    plugin_id: p.into(),
                })
                .collect(),
            edges: [(1, 2, 0), (1, 3, 1), (2, 3, 0), (3, 4, 0), (1, 5, 0)]
                .into_iter()
                .map(|(a, b, port)| Connection {
                    from: Endpoint {
                        node: id(a),
                        port: PortId(0),
                    },
                    to: Endpoint {
                        node: id(b),
                        port: PortId(port),
                    },
                })
                .collect(),
        };
        Prepared::new(&patch, &registry, &[id(4), id(5)]).unwrap()
    }
    #[test]
    fn fanout_is_one_source_call_and_mixer_sums_without_modifying_other_branch() {
        let mut graph = fixture(0);
        let mut source_calls = 0;
        let mut received = Vec::new();
        graph
            .process(127, |id, role, inputs, outputs, frames| {
                match role {
                    Role::Source => {
                        source_calls += 1;
                        for channel in &mut outputs[0] {
                            channel[..frames].fill(0.25);
                        }
                    }
                    Role::Effect => {
                        for (to, from) in outputs[0].iter_mut().zip(&inputs[0]) {
                            for (v, x) in to[..frames].iter_mut().zip(&from[..frames]) {
                                *v = *x * 0.5;
                            }
                        }
                    }
                    Role::Consumer => {
                        assert!(outputs.is_empty());
                        received.push((id, inputs[0][0][0]));
                    }
                    Role::Mixer => unreachable!(),
                }
                true
            })
            .unwrap();
        assert_eq!(source_calls, 1);
        assert_eq!(received, [(id(4), 0.375), (id(5), 0.25)]);
    }
    #[test]
    fn impulse_reconverges_in_phase_with_variable_frames_and_no_hidden_clipping() {
        let mut graph = fixture(4);
        let mut clock = 0;
        let mut delay = [0.0; 4];
        let mut cursor = 0;
        let mut result = Vec::new();
        for size in [3, 1, 7, 480] {
            graph
                .process(size, |id, role, inputs, outputs, frames| {
                    match role {
                        Role::Source => {
                            let [left, right] = &mut outputs[0];
                            for (l, r) in left.iter_mut().zip(right).take(frames) {
                                *l = if clock == 0 { 1.0 } else { 0.0 };
                                *r = *l;
                                clock += 1;
                            }
                        }
                        Role::Effect => {
                            for i in 0..frames {
                                outputs[0][0][i] = delay[cursor];
                                outputs[0][1][i] = delay[cursor];
                                delay[cursor] = inputs[0][0][i];
                                cursor = (cursor + 1) % 4;
                            }
                        }
                        Role::Consumer if id == self::id(4) => {
                            result.extend_from_slice(&inputs[0][0][..frames])
                        }
                        _ => {}
                    }
                    true
                })
                .unwrap();
        }
        assert_eq!(result[4], 2.0);
        assert_eq!(result.iter().filter(|v| **v != 0.0).count(), 1);
    }
    #[test]
    fn invalid_frames_and_nonfinite_source_never_reach_consumers() {
        let mut graph = fixture(0);
        let mut called = false;
        assert_eq!(
            graph.process(481, |_, _, _, _, _| {
                called = true;
                true
            }),
            Err(ProcessError::InvalidFrames)
        );
        assert!(!called);
        assert_eq!(
            graph.process(1, |_, role, _, outputs, _| {
                assert_eq!(role, Role::Source);
                outputs[0][0][0] = f32::NAN;
                true
            }),
            Err(ProcessError::NonFinite(id(1)))
        );
    }
}
