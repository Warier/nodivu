//! Fixed-size adapter of the routing compiler for atomic publication to WASAPI.
use crate::{ApiError, graph::*, routing::*};

pub const WORDS: usize = 3 + MAX_NODES * (3 + MAX_PORTS);
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Input {
    /// One-based producing step; zero means disconnected silence.
    pub source: u8,
    pub delay: u16,
    pub offset: u16,
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Step {
    pub id: Option<NodeId>,
    /// 1 capture, 2 tone, 3 gain, 4 external, 5 mixer, 6 output, 7 meter, 8 external consumer.
    pub kind: u8,
    pub slot: u8,
    pub enabled: bool,
    pub inputs: [Input; MAX_PORTS],
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct LivePlan {
    pub count: u8,
    pub delay_frames: u32,
    pub latency_frames: u32,
    pub steps: [Step; MAX_NODES],
}
impl LivePlan {
    pub fn words(&self) -> [u64; WORDS] {
        let mut words = [0; WORDS];
        words[0] = self.count.into();
        words[1] = self.delay_frames.into();
        words[2] = self.latency_frames.into();
        for (step, w) in self
            .steps
            .iter()
            .zip(words[3..].as_chunks_mut::<{ 3 + MAX_PORTS }>().0.iter_mut())
        {
            let id = step.id.map_or(0, |id| id.0.as_u128());
            w[0] = (id >> 64) as u64;
            w[1] = id as u64;
            w[2] = u64::from(step.kind)
                | (u64::from(step.slot) << 8)
                | (u64::from(step.enabled) << 16)
                | (u64::from(step.id.is_some()) << 17);
            for (input, word) in step.inputs.iter().zip(&mut w[3..]) {
                *word = u64::from(input.source)
                    | (u64::from(input.delay) << 8)
                    | (u64::from(input.offset) << 24);
            }
        }
        words
    }
    pub fn from_words(words: &[u64; WORDS]) -> Self {
        let mut plan = Self {
            count: words[0] as u8,
            delay_frames: words[1] as u32,
            latency_frames: words[2] as u32,
            ..Self::default()
        };
        for (step, w) in plan
            .steps
            .iter_mut()
            .zip(words[3..].as_chunks::<{ 3 + MAX_PORTS }>().0.iter())
        {
            step.id = (w[2] & (1 << 17) != 0).then(|| {
                NodeId(uuid::Uuid::from_u128(
                    (u128::from(w[0]) << 64) | u128::from(w[1]),
                ))
            });
            step.kind = w[2] as u8;
            step.slot = (w[2] >> 8) as u8;
            step.enabled = w[2] & (1 << 16) != 0;
            for (input, word) in step.inputs.iter_mut().zip(&w[3..]) {
                *input = Input {
                    source: *word as u8,
                    delay: (*word >> 8) as u16,
                    offset: (*word >> 24) as u16,
                };
            }
        }
        plan
    }
    pub fn validate(&self) -> Result<(), ApiError> {
        if usize::from(self.count) > MAX_NODES
            || self.delay_frames > MAX_DELAY_FRAMES
            || self.latency_frames > MAX_DELAY_FRAMES
        {
            return Err(ApiError::invalid("Plano DAG excede orçamento."));
        }
        for (i, s) in self.steps[..usize::from(self.count)].iter().enumerate() {
            if s.id.is_none()
                || !(1..=8).contains(&s.kind)
                || (s.kind == 3 && usize::from(s.slot) >= MAX_GAIN_NODES)
                || self.steps[..i].iter().any(|p| p.id == s.id)
                || s.inputs.iter().any(|p| {
                    usize::from(p.source) > i
                        || u32::from(p.offset) + u32::from(p.delay) > self.delay_frames
                })
            {
                return Err(ApiError::invalid("Passo DAG inválido."));
            }
        }
        Ok(())
    }
}

impl Graph {
    /// Port declarations are built by the host; plugin latency is supplied by its backend.
    pub fn routing_patch(
        &self,
        latency: impl Fn(&str) -> u32,
    ) -> (Patch, Vec<Descriptor>, Vec<NodeId>) {
        let mut registry = Vec::new();
        let mut roots = Vec::new();
        let mut nodes = Vec::new();
        for node in &self.nodes {
            let name = node.id.0.to_string();
            let (role, inputs, outputs, delay) = match &node.block {
                Block::Capture { .. } | Block::Tone { .. } => (Role::Source, 0, 1, 0),
                Block::Output { endpoint_id } => {
                    if endpoint_id.is_some() {
                        roots.push(node.id);
                    }
                    (Role::Consumer, 1, 0, 0)
                }
                Block::Meter => {
                    roots.push(node.id);
                    (Role::Consumer, 1, 0, 0)
                }
                Block::Mixer => (Role::Mixer, 4, 1, 0),
                Block::Gain { .. } => (Role::Effect, 1, 1, 0),
                Block::Plugin { consumer: true, .. } => {
                    roots.push(node.id);
                    (Role::Consumer, 1, 0, 0)
                }
                Block::Plugin {
                    source, plugin_id, ..
                } => (
                    if *source { Role::Source } else { Role::Effect },
                    usize::from(!source),
                    1,
                    latency(plugin_id),
                ),
            };
            registry.push(Descriptor {
                plugin_id: name.clone(),
                role,
                inputs: (0..inputs).map(|v| PortId(v as u32)).collect(),
                outputs: (0..outputs).map(|v| PortId(v as u32)).collect(),
                latency_frames: delay,
            });
            nodes.push(Instance {
                id: node.id,
                plugin_id: name,
            });
        }
        let edges = self
            .edges
            .iter()
            .map(|e| Connection {
                from: Endpoint {
                    node: e.from,
                    port: PortId(e.from_port),
                },
                to: Endpoint {
                    node: e.to,
                    port: PortId(e.to_port),
                },
            })
            .collect();
        (
            Patch {
                schema_version: 2,
                nodes,
                edges,
            },
            registry,
            roots,
        )
    }
    pub fn live_plan(
        &self,
        muted: bool,
        latency: impl Fn(&str) -> u32,
    ) -> Result<RenderPlan, ApiError> {
        let (patch, registry, roots) = self.routing_patch(latency);
        let compiled = patch.compile(&registry, &roots)?;
        let mut plan = RenderPlan::default();
        for (i, node) in self
            .nodes
            .iter()
            .filter(|n| matches!(n.block, Block::Gain { .. }))
            .enumerate()
        {
            if i >= MAX_GAIN_NODES {
                return Err(ApiError::invalid("Até oito ganhos."));
            }
            if let Block::Gain { gain_db, bypass } = node.block {
                plan.gains[i] = Some(GainStep {
                    id: node.id,
                    amplitude: 10.0_f32.powf(f32::from(gain_db) / 20.0),
                    bypass,
                });
            }
        }
        let mut live = LivePlan {
            count: compiled.steps.len() as u8,
            delay_frames: compiled.delay_frames,
            latency_frames: compiled
                .terminal_latencies
                .iter()
                .map(|p| p.1)
                .max()
                .unwrap_or(0),
            ..LivePlan::default()
        };
        let mut offset = 0;
        let mut externals = 0;
        for (i, step) in compiled.steps.iter().enumerate() {
            let node = self.node(step.id)?;
            let (kind, enabled, slot) = match node.block {
                Block::Capture { ref endpoint_id } => (1, endpoint_id.is_some(), 0),
                Block::Tone { enabled } => (2, enabled, 0),
                Block::Gain { .. } => (
                    3,
                    true,
                    plan.gains
                        .iter()
                        .position(|g| g.is_some_and(|g| g.id == node.id))
                        .ok_or_else(|| ApiError::invalid("Ganho ausente"))?
                        as u8,
                ),
                Block::Plugin { consumer, .. } => {
                    if externals >= MAX_PLUGIN_NODES {
                        return Err(ApiError::invalid("Até oito plugins."));
                    }
                    plan.external[externals] = Some(node.id);
                    externals += 1;
                    (if consumer { 8 } else { 4 }, true, 0)
                }
                Block::Mixer => (5, true, 0),
                Block::Output { .. } => (6, true, 0),
                Block::Meter => (7, true, 0),
            };
            let target = &mut live.steps[i];
            *target = Step {
                id: Some(node.id),
                kind,
                slot,
                enabled,
                ..Step::default()
            };
            for (j, input) in step.inputs.iter().enumerate() {
                let source = if let Some(buffer) = input.buffer {
                    compiled.steps[..i]
                        .iter()
                        .position(|s| s.outputs.iter().any(|o| o.buffer == buffer))
                        .ok_or_else(|| ApiError::invalid("Buffer ausente"))?
                        + 1
                } else {
                    0
                };
                target.inputs[j] = Input {
                    source: source as u8,
                    delay: input.delay_frames as u16,
                    offset: offset as u16,
                };
                offset += input.delay_frames;
            }
        }
        let source = if live.steps.iter().any(|s| s.kind == 5) {
            4
        } else if live.steps.iter().any(|s| s.kind == 1 && s.enabled) {
            1
        } else if live.steps.iter().any(|s| s.kind == 2 && s.enabled) {
            2
        } else if externals > 0 {
            3
        } else {
            0
        };
        plan.signal = SignalPlan {
            source,
            amplitude: if muted { 0.0 } else { 1.0 },
        };
        plan.live = Some(live);
        plan.validate()?;
        Ok(plan)
    }
}
