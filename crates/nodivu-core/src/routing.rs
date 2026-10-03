//! Routing contract compiled off the audio thread; live_routing adapts the app document.
//! Capabilities come from the host registry, never from mutable project metadata.
use crate::{ApiError, graph::NodeId};
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

pub const MAX_NODES: usize = 32;
pub const MAX_EDGES: usize = 64;
pub const MAX_PORTS: usize = 8;
pub const MAX_BUFFERS: usize = 64;
/// Aggregate stereo-frame budget across compensation lines, not per edge.
pub const MAX_DELAY_FRAMES: u32 = 48_000;

#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Serialize, Deserialize)]
#[serde(transparent)]
pub struct PortId(pub u32);
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum Role {
    Source,
    Effect,
    Mixer,
    Consumer,
}

/// Initial multi-port profile: every port is stereo float32 planar, 48 kHz.
#[derive(Clone, Debug)]
pub struct Descriptor {
    pub plugin_id: String,
    pub role: Role,
    pub inputs: Vec<PortId>,
    pub outputs: Vec<PortId>,
    pub latency_frames: u32,
}
impl Descriptor {
    fn validate(&self) -> Result<(), ApiError> {
        let valid_ports = |ports: &[PortId]| {
            ports.len() <= MAX_PORTS
                && ports
                    .iter()
                    .enumerate()
                    .all(|(i, p)| !ports[..i].contains(p) && p.0 != u32::MAX)
        };
        let shape = match self.role {
            Role::Source => self.inputs.is_empty() && !self.outputs.is_empty(),
            Role::Effect => !self.inputs.is_empty() && !self.outputs.is_empty(),
            Role::Mixer => {
                (2..=MAX_PORTS).contains(&self.inputs.len())
                    && self.outputs.len() == 1
                    && self.latency_frames == 0
            }
            Role::Consumer => !self.inputs.is_empty() && self.outputs.is_empty(),
        };
        if self.plugin_id.is_empty()
            || self.plugin_id.len() > 128
            || !valid_ports(&self.inputs)
            || !valid_ports(&self.outputs)
            || !shape
            || self.latency_frames > MAX_DELAY_FRAMES
        {
            return Err(ApiError::invalid(
                "Descritor/portas/latência de roteamento inválidos.",
            ));
        }
        Ok(())
    }
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Instance {
    pub id: NodeId,
    pub plugin_id: String,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Endpoint {
    pub node: NodeId,
    pub port: PortId,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Connection {
    pub from: Endpoint,
    pub to: Endpoint,
}
#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Patch {
    pub schema_version: u32,
    pub nodes: Vec<Instance>,
    pub edges: Vec<Connection>,
}

#[derive(Clone, Debug)]
pub struct InputBinding {
    pub port: PortId,
    pub buffer: Option<usize>,
    pub delay_frames: u32,
}
#[derive(Clone, Debug)]
pub struct OutputBinding {
    pub port: PortId,
    pub buffer: usize,
}
#[derive(Clone, Debug)]
pub struct Step {
    pub id: NodeId,
    pub role: Role,
    pub inputs: Vec<InputBinding>,
    pub outputs: Vec<OutputBinding>,
    pub arrival_frames: u32,
}
#[derive(Clone, Debug)]
pub struct Compiled {
    pub steps: Vec<Step>,
    pub buffer_count: usize,
    pub delay_frames: u32,
    pub terminal_latencies: Vec<(NodeId, u32)>,
}

impl Patch {
    /// Roots are active consumers selected by the control layer (output/armed recorder/etc.).
    /// Validate the whole document, then execute only ancestors of those consumers.
    pub fn compile(&self, registry: &[Descriptor], roots: &[NodeId]) -> Result<Compiled, ApiError> {
        if self.schema_version != 2
            || self.nodes.len() > MAX_NODES
            || self.edges.len() > MAX_EDGES
            || registry.len() > 64
            || roots.len() > MAX_NODES
        {
            return Err(ApiError::invalid(
                "Roteamento v2 excede limites de nós, tipos ou fios.",
            ));
        }
        let mut types = BTreeMap::new();
        for d in registry {
            d.validate()?;
            if types.insert(d.plugin_id.as_str(), d).is_some() {
                return Err(ApiError::invalid("Tipo duplicado no catálogo."));
            }
        }
        let mut nodes = BTreeMap::new();
        for n in &self.nodes {
            let d = types
                .get(n.plugin_id.as_str())
                .ok_or_else(|| ApiError::invalid("Tipo ausente no catálogo."))?;
            if nodes.insert(n.id.0, (*d, n.id)).is_some() {
                return Err(ApiError::invalid("UUID duplicado."));
            }
        }
        let mut degree: BTreeMap<_, usize> = nodes.keys().map(|id| (*id, 0)).collect();
        for (i, e) in self.edges.iter().enumerate() {
            let from = nodes
                .get(&e.from.node.0)
                .ok_or_else(|| ApiError::invalid("Origem ausente."))?
                .0;
            let to = nodes
                .get(&e.to.node.0)
                .ok_or_else(|| ApiError::invalid("Destino ausente."))?
                .0;
            if e.from.node == e.to.node
                || !from.outputs.contains(&e.from.port)
                || !to.inputs.contains(&e.to.port)
            {
                return Err(ApiError::invalid("Direção ou ID de porta inválidos."));
            }
            if self.edges[..i].iter().any(|p| p.to == e.to) {
                return Err(ApiError::invalid(
                    "Uma conexão por entrada. Para somar sinais, use portas de um Mixer.",
                ));
            }
            if let Some(value) = degree.get_mut(&e.to.node.0) {
                *value += 1;
            }
        }
        // Stable UUID order means reorder of JSON arrays does not change summation/execution.
        let mut order = Vec::new();
        let mut visited = BTreeSet::new();
        while let Some((&id, _)) = degree
            .iter()
            .find(|(id, d)| **d == 0 && !visited.contains(*id))
        {
            visited.insert(id);
            order.push(id);
            for e in &self.edges {
                if e.from.node.0 == id
                    && let Some(value) = degree.get_mut(&e.to.node.0)
                {
                    *value -= 1;
                }
            }
        }
        if order.len() != nodes.len() {
            return Err(ApiError::invalid(
                "Ciclo de áudio: todos os ramos precisam ser acíclicos.",
            ));
        }
        let mut active = BTreeSet::new();
        for (i, root) in roots.iter().enumerate() {
            if roots[..i].contains(root)
                || nodes
                    .get(&root.0)
                    .is_none_or(|n| n.0.role != Role::Consumer)
            {
                return Err(ApiError::invalid(
                    "Terminal ativo deve ser consumidor único existente.",
                ));
            }
            active.insert(root.0);
        }
        for id in order.iter().rev() {
            if active.contains(id) {
                for e in &self.edges {
                    if e.to.node.0 == *id {
                        active.insert(e.from.node.0);
                    }
                }
            }
        }
        let mut result = Compiled {
            steps: Vec::new(),
            buffer_count: 0,
            delay_frames: 0,
            terminal_latencies: Vec::new(),
        };
        let mut buffers = BTreeMap::new();
        let mut arrivals = BTreeMap::new();
        for id in order.into_iter().filter(|id| active.contains(id)) {
            let (descriptor, node) = nodes[&id];
            let arrival = self
                .edges
                .iter()
                .filter(|e| e.to.node.0 == id)
                .map(|e| arrivals[&e.from.node.0])
                .max()
                .unwrap_or(0_u32);
            let mut inputs = descriptor.inputs.clone();
            inputs.sort();
            let mut outputs = descriptor.outputs.clone();
            outputs.sort();
            let mut step = Step {
                id: node,
                role: descriptor.role,
                inputs: Vec::new(),
                outputs: Vec::new(),
                arrival_frames: arrival,
            };
            for port in inputs {
                let edge = self
                    .edges
                    .iter()
                    .find(|e| e.to.node.0 == id && e.to.port == port);
                let (buffer, delay) = if let Some(e) = edge {
                    (
                        Some(buffers[&(e.from.node.0, e.from.port)]),
                        arrival - arrivals[&e.from.node.0],
                    )
                } else {
                    (None, 0)
                };
                result.delay_frames = result
                    .delay_frames
                    .checked_add(delay)
                    .ok_or_else(|| ApiError::invalid("Compensação excedeu limite."))?;
                if result.delay_frames > MAX_DELAY_FRAMES {
                    return Err(ApiError::invalid(
                        "Buffers de compensação excedem orçamento agregado de 48000 frames.",
                    ));
                }
                step.inputs.push(InputBinding {
                    port,
                    buffer,
                    delay_frames: delay,
                });
            }
            for port in outputs {
                if result.buffer_count == MAX_BUFFERS {
                    return Err(ApiError::invalid("Grafo excede 64 buffers de saída."));
                }
                buffers.insert((id, port), result.buffer_count);
                step.outputs.push(OutputBinding {
                    port,
                    buffer: result.buffer_count,
                });
                result.buffer_count += 1;
            }
            let latency = arrival
                .checked_add(descriptor.latency_frames)
                .filter(|v| *v <= MAX_DELAY_FRAMES)
                .ok_or_else(|| {
                    ApiError::invalid("Caminho excede latência máxima de 48000 frames.")
                })?;
            arrivals.insert(id, latency);
            if descriptor.role == Role::Consumer {
                result.terminal_latencies.push((node, latency));
            }
            result.steps.push(step);
        }
        Ok(result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn id(i: u128) -> NodeId {
        NodeId(uuid::Uuid::from_u128(i))
    }
    fn d(
        name: &str,
        role: Role,
        inputs: &[u32],
        outputs: &[u32],
        latency_frames: u32,
    ) -> Descriptor {
        Descriptor {
            plugin_id: name.into(),
            role,
            inputs: inputs.iter().copied().map(PortId).collect(),
            outputs: outputs.iter().copied().map(PortId).collect(),
            latency_frames,
        }
    }
    fn edge(from: u128, to: u128, port: u32) -> Connection {
        Connection {
            from: Endpoint {
                node: id(from),
                port: PortId(0),
            },
            to: Endpoint {
                node: id(to),
                port: PortId(port),
            },
        }
    }
    fn fixture() -> (Patch, Vec<Descriptor>) {
        (
            Patch {
                schema_version: 2,
                nodes: [
                    (1, "source"),
                    (2, "effect"),
                    (3, "mix"),
                    (4, "consumer"),
                    (5, "consumer"),
                ]
                .into_iter()
                .map(|(i, p)| Instance {
                    id: id(i),
                    plugin_id: p.into(),
                })
                .collect(),
                edges: vec![
                    edge(1, 2, 0),
                    edge(1, 3, 1),
                    edge(2, 3, 0),
                    edge(3, 4, 0),
                    edge(1, 5, 0),
                ],
            },
            vec![
                d("source", Role::Source, &[], &[0], 0),
                d("effect", Role::Effect, &[0], &[0], 960),
                d("mix", Role::Mixer, &[0, 1], &[0], 0),
                d("consumer", Role::Consumer, &[0], &[], 0),
            ],
        )
    }
    #[test]
    fn fanout_executes_once_and_reconvergence_aligns_latency() {
        let (p, r) = fixture();
        let c = p.compile(&r, &[id(4), id(5)]).unwrap();
        assert_eq!(c.steps.iter().filter(|s| s.id == id(1)).count(), 1);
        assert_eq!(c.steps.len(), 5);
        assert_eq!(c.delay_frames, 960);
        let mix = c.steps.iter().find(|s| s.role == Role::Mixer).unwrap();
        assert_eq!(
            mix.inputs
                .iter()
                .map(|p| p.delay_frames)
                .collect::<Vec<_>>(),
            [0, 960]
        );
        assert_eq!(c.terminal_latencies, [(id(4), 960), (id(5), 0)]);
        assert_eq!(p.compile(&r, &[id(5)]).unwrap().steps.len(), 2);
    }
    #[test]
    fn rejects_cycle_on_nonfirst_branch_duplicate_sink_and_forged_ports() {
        let (mut p, r) = fixture();
        p.edges.push(edge(3, 2, 0));
        assert!(p.compile(&r, &[id(4)]).is_err());
        let (mut p, r) = fixture();
        p.edges.retain(|e| e.to.node != id(2));
        p.edges.push(edge(3, 2, 0));
        assert!(p.compile(&r, &[id(4)]).is_err());
        let (mut p, r) = fixture();
        p.edges[0].from.port = PortId(99);
        assert!(p.compile(&r, &[id(4)]).is_err());
    }
    #[test]
    fn order_is_stable_and_compensation_is_bounded() {
        let (mut p, mut r) = fixture();
        let before = p.compile(&r, &[id(4)]).unwrap();
        p.nodes.reverse();
        p.edges.reverse();
        let after = p.compile(&r, &[id(4)]).unwrap();
        assert_eq!(
            before.steps.iter().map(|s| s.id).collect::<Vec<_>>(),
            after.steps.iter().map(|s| s.id).collect::<Vec<_>>()
        );
        // Root validation must fail with an otherwise valid registry.
        assert!(p.compile(&r, &[id(1)]).is_err());
        // Each delay fits individually, but two reconvergences exceed the shared budget.
        r[1].latency_frames = 30_000;
        p.nodes.push(Instance {
            id: id(6),
            plugin_id: "mix".into(),
        });
        p.edges.retain(|e| e.to.node != id(5));
        p.edges
            .extend([edge(1, 6, 0), edge(2, 6, 1), edge(6, 5, 0)]);
        assert_eq!(p.compile(&r, &[id(4)]).unwrap().delay_frames, 30_000);
        assert!(p.compile(&r, &[id(4), id(5)]).is_err());
        r[1].latency_frames = MAX_DELAY_FRAMES + 1;
        assert!(p.compile(&r, &[id(4)]).is_err());
    }
}
