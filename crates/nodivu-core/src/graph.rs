//! Documento do app: buses estéreo, portas estáveis e DAG limitado.
use crate::{ApiError, DeviceId, ErrorCode, GainDb};
use serde::{Deserialize, Serialize};
use uuid::Uuid;

#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct NodeId(pub Uuid);
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(tag = "kind", rename_all = "snake_case", deny_unknown_fields)]
pub enum Block {
    Mixer,
    Meter,
    Capture {
        endpoint_id: Option<DeviceId>,
    },
    Output {
        endpoint_id: Option<DeviceId>,
    },
    Gain {
        gain_db: GainDb,
        bypass: bool,
    },
    Tone {
        enabled: bool,
    },
    Plugin {
        #[serde(default)]
        source: bool,
        #[serde(default)]
        consumer: bool,
        plugin_id: String,
        #[serde(deserialize_with = "parameter_map")]
        parameters: std::collections::BTreeMap<u32, f64>,
        bypass: bool,
    },
}
fn parameter_map<'de, D: serde::Deserializer<'de>>(
    deserializer: D,
) -> Result<std::collections::BTreeMap<u32, f64>, D::Error> {
    // O enum tagged passa por Content; converter chaves JSON explicitamente mantém IDs tipados.
    let raw = std::collections::BTreeMap::<String, f64>::deserialize(deserializer)?;
    raw.into_iter()
        .map(|(key, value)| {
            let id = key.parse::<u32>().map_err(serde::de::Error::custom)?;
            if id.to_string() != key {
                return Err(serde::de::Error::custom("ID de parâmetro não canônico"));
            }
            Ok((id, value))
        })
        .collect()
}
impl Block {
    pub fn has_input(&self) -> bool {
        matches!(
            self,
            Self::Mixer
                | Self::Meter
                | Self::Output { .. }
                | Self::Gain { .. }
                | Self::Plugin { source: false, .. }
        )
    }
    pub fn has_output(&self) -> bool {
        !matches!(
            self,
            Self::Output { .. } | Self::Meter | Self::Plugin { consumer: true, .. }
        )
    }
    pub fn kind(&self) -> &'static str {
        match self {
            Self::Mixer => "mixer",
            Self::Meter => "meter",
            Self::Capture { .. } => "capture",
            Self::Output { .. } => "output",
            Self::Gain { .. } => "gain",
            Self::Tone { .. } => "tone",
            Self::Plugin { .. } => "plugin",
        }
    }
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Node {
    pub id: NodeId,
    pub block: Block,
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Edge {
    #[serde(default)]
    pub from_port: u32,
    #[serde(default)]
    pub to_port: u32,
    pub from: NodeId,
    pub to: NodeId,
}
#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Graph {
    pub schema_version: u32,
    pub nodes: Vec<Node>,
    pub edges: Vec<Edge>,
}
impl Default for Graph {
    fn default() -> Self {
        Self {
            schema_version: 1,
            nodes: vec![],
            edges: vec![],
        }
    }
}
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SignalPlan {
    pub source: u32,
    pub amplitude: f32,
}
impl Default for SignalPlan {
    fn default() -> Self {
        Self {
            source: 0,
            amplitude: 0.0,
        }
    }
}
impl SignalPlan {
    // Um único valor atômico publica fonte e amplitude coerentemente entre threads.
    pub fn packed(self) -> u64 {
        (u64::from(self.source) << 32) | u64::from(self.amplitude.to_bits())
    }
    pub fn unpack(value: u64) -> Self {
        Self {
            source: (value >> 32) as u32,
            amplitude: f32::from_bits(value as u32),
        }
    }
}

pub const MAX_GAIN_NODES: usize = 8;
pub const MAX_PLUGIN_NODES: usize = 8;
pub const MAX_PROCESSORS: usize = MAX_GAIN_NODES + MAX_PLUGIN_NODES;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct GainStep {
    pub id: NodeId,
    pub amplitude: f32,
    pub bypass: bool,
}

/// Plano pequeno e copiável: contém valores, nunca buffers/handles de áudio.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct RenderPlan {
    pub live: Option<crate::live_routing::LivePlan>,
    pub external: [Option<NodeId>; MAX_PLUGIN_NODES],
    pub signal: SignalPlan,
    pub gains: [Option<GainStep>; MAX_GAIN_NODES],
    /// 0..8 indexes gains; 8..16 indexes external instances.
    pub order: [u8; MAX_PROCESSORS],
    pub count: u8,
}
impl RenderPlan {
    pub fn validate(&self) -> Result<(), ApiError> {
        if let Some(live) = self.live {
            live.validate()?;
            if self.signal.source > 4
                || live.steps[..usize::from(live.count)].iter().any(|step| {
                    (step.kind == 3 && self.gains[usize::from(step.slot)].map(|g| g.id) != step.id)
                        || ([4, 8].contains(&step.kind) && !self.external.contains(&step.id))
                })
            {
                return Err(ApiError::invalid("Identidade de processador DAG inválida."));
            }
            let mut legacy = *self;
            legacy.live = None;
            legacy.signal.source = 3;
            legacy.count = 0;
            return legacy.validate();
        }
        if (self.signal.source == 0
            && (self.signal.amplitude != 0.0
                || self.count != 0
                || self.external.iter().any(Option::is_some)))
            || self.count as usize > MAX_PROCESSORS
            || self.signal.source > 3
            || !self.signal.amplitude.is_finite()
            || !(0.0..=16.0).contains(&self.signal.amplitude)
        {
            return Err(ApiError::invalid("Plano de áudio fora dos limites."));
        }
        for (i, gain) in self.gains.iter().enumerate() {
            if let Some(gain) = gain
                && (!gain.amplitude.is_finite()
                    || !(0.0..=16.0).contains(&gain.amplitude)
                    || self.gains[..i]
                        .iter()
                        .flatten()
                        .any(|other| other.id == gain.id))
            {
                return Err(ApiError::invalid("Instância de ganho inválida no plano."));
            }
        }
        for (i, id) in self.external.iter().enumerate() {
            if let Some(id) = id
                && (self.external[..i].contains(&Some(*id))
                    || self.gains.iter().flatten().any(|g| g.id == *id))
            {
                return Err(ApiError::invalid("Identidade de processador duplicada."));
            }
        }
        for (i, slot) in self.order[..usize::from(self.count)].iter().enumerate() {
            if self.processor_id(*slot).is_none() || self.order[..i].contains(slot) {
                return Err(ApiError::invalid("Ordem de execução inválida."));
            }
        }
        Ok(())
    }
    pub fn same_route(&self, other: &Self) -> bool {
        if self.live.is_some() || other.live.is_some() {
            return self.live == other.live;
        }
        if self.count as usize > MAX_PROCESSORS || other.count as usize > MAX_PROCESSORS {
            return false;
        }
        self.signal.source == other.signal.source
            && self.count == other.count
            && (0..usize::from(self.count))
                .all(|i| self.processor_id(self.order[i]) == other.processor_id(other.order[i]))
    }
    pub fn processor_id(&self, index: u8) -> Option<NodeId> {
        let index = usize::from(index);
        if index < MAX_GAIN_NODES {
            self.gains[index].map(|g| g.id)
        } else {
            self.external.get(index - MAX_GAIN_NODES).copied().flatten()
        }
    }
}
impl From<SignalPlan> for RenderPlan {
    fn from(signal: SignalPlan) -> Self {
        Self {
            signal,
            ..Self::default()
        }
    }
}
impl Graph {
    /// Chamado no controle depois de validate; desconexão deixa nós disponíveis sem executá-los.
    pub fn render_plan(&self, muted: bool) -> RenderPlan {
        let mut plan = RenderPlan::default();
        for (slot, node) in self
            .nodes
            .iter()
            .filter(|n| matches!(n.block, Block::Gain { .. }))
            .take(MAX_GAIN_NODES)
            .enumerate()
        {
            if let Block::Gain { gain_db, bypass } = node.block {
                plan.gains[slot] = Some(GainStep {
                    id: node.id,
                    amplitude: 10.0_f32.powf(f32::from(gain_db) / 20.0),
                    bypass,
                });
            }
        }
        let Some(output) = self.nodes.iter().find(|n| {
            matches!(
                n.block,
                Block::Output {
                    endpoint_id: Some(_)
                }
            )
        }) else {
            return plan;
        };
        let mut current = output.id;
        let mut reversed = [0_u8; MAX_PROCESSORS];
        let mut count = 0;
        let mut external = [None; MAX_PLUGIN_NODES];
        let mut plugin_count = 0;
        for _ in 0..self.nodes.len() {
            let Some(edge) = self.edges.iter().find(|e| e.to == current) else {
                return plan;
            };
            let Ok(node) = self.node(edge.from) else {
                return plan;
            };
            match node.block {
                Block::Plugin { .. } => {
                    if count >= MAX_PROCESSORS || plugin_count >= MAX_PLUGIN_NODES {
                        return plan;
                    }
                    external[plugin_count] = Some(node.id);
                    reversed[count] = (MAX_GAIN_NODES + plugin_count) as u8;
                    plugin_count += 1;
                    count += 1;
                    if matches!(node.block, Block::Plugin { source: true, .. }) {
                        plan.signal = SignalPlan {
                            source: 3,
                            amplitude: if muted { 0.0 } else { 1.0 },
                        };
                        plan.count = count as u8;
                        plan.external = external;
                        for (to, from) in plan.order.iter_mut().zip(reversed[..count].iter().rev())
                        {
                            *to = *from;
                        }
                        return plan;
                    }
                }
                Block::Gain { .. } => {
                    let Some(slot) = plan
                        .gains
                        .iter()
                        .position(|g| g.is_some_and(|g| g.id == node.id))
                    else {
                        return plan;
                    };
                    if count >= MAX_PROCESSORS {
                        return plan;
                    }
                    reversed[count] = slot as u8;
                    count += 1;
                }
                Block::Capture {
                    endpoint_id: Some(_),
                }
                | Block::Tone { enabled: true } => {
                    plan.signal = SignalPlan {
                        source: if matches!(node.block, Block::Capture { .. }) {
                            1
                        } else {
                            2
                        },
                        amplitude: if muted { 0.0 } else { 1.0 },
                    };
                    plan.count = count as u8;
                    plan.external = external;
                    for (destination, source) in
                        plan.order.iter_mut().zip(reversed[..count].iter().rev())
                    {
                        *destination = *source;
                    }
                    return plan;
                }
                _ => return plan,
            }
            current = node.id;
        }
        plan
    }
    pub fn node(&self, id: NodeId) -> Result<&Node, ApiError> {
        self.nodes
            .iter()
            .find(|n| n.id == id)
            .ok_or_else(|| ApiError::new(ErrorCode::NotFound, "Bloco não encontrado."))
    }
    pub fn validate(&self) -> Result<(), ApiError> {
        if ![1, 2].contains(&self.schema_version) || self.nodes.len() > 32 || self.edges.len() > 64
        {
            return Err(ApiError::invalid("Grafo v1: até 19 blocos e 18 fios."));
        }
        for (i, n) in self.nodes.iter().enumerate() {
            if let Block::Plugin {
                plugin_id,
                parameters,
                ..
            } = &n.block
                && (plugin_id.is_empty()
                    || plugin_id.len() > 128
                    || parameters.len() > 32
                    || parameters.values().any(|v| !v.is_finite()))
            {
                return Err(ApiError::invalid(
                    "Referência ou parâmetros de plugin inválidos.",
                ));
            }
            if self.nodes[..i].iter().any(|other| other.id == n.id) {
                return Err(ApiError::invalid("ID de bloco duplicado."));
            }
        }
        for kind in ["capture", "output", "tone"] {
            if self.nodes.iter().filter(|n| n.block.kind() == kind).count() > 1 {
                return Err(ApiError::invalid(
                    "Uma instância de cada fonte e saída nesta versão.",
                ));
            }
        }
        if self
            .nodes
            .iter()
            .filter(|n| n.block.kind() == "gain")
            .count()
            > 8
        {
            return Err(ApiError::invalid("Até oito blocos de ganho."));
        }
        if self
            .nodes
            .iter()
            .filter(|n| n.block.kind() == "plugin")
            .count()
            > MAX_PLUGIN_NODES
        {
            return Err(ApiError::invalid("Até oito instâncias de plugin."));
        }
        let (patch, registry, roots) = self.routing_patch(|_| 0);
        patch.compile(&registry, &roots)?;
        Ok(())
    }
    pub fn endpoint(&self, capture: bool) -> Option<DeviceId> {
        self.nodes.iter().find_map(|n| match &n.block {
            Block::Capture { endpoint_id } if capture => endpoint_id.clone(),
            Block::Output { endpoint_id } if !capture => endpoint_id.clone(),
            _ => None,
        })
    }
    pub fn signal(&self, muted: bool) -> SignalPlan {
        if muted {
            return SignalPlan::default();
        }
        let Some(output) = self.nodes.iter().find(|n| {
            matches!(
                n.block,
                Block::Output {
                    endpoint_id: Some(_)
                }
            )
        }) else {
            return SignalPlan::default();
        };
        let mut current = output.id;
        // Saída em unidade: somente os blocos de ganho alteram a amplitude.
        let mut db = 0.0_f32;
        for _ in 0..self.nodes.len() {
            let Some(edge) = self.edges.iter().find(|e| e.to == current) else {
                return SignalPlan::default();
            };
            let Ok(node) = self.node(edge.from) else {
                return SignalPlan::default();
            };
            match &node.block {
                Block::Plugin { source: true, .. } => {
                    return SignalPlan {
                        source: 3,
                        amplitude: 10.0_f32.powf(db / 20.0),
                    };
                }
                Block::Plugin { .. } => {}
                Block::Gain { gain_db, bypass } => {
                    if !bypass {
                        db += f32::from(*gain_db);
                    }
                }
                Block::Capture {
                    endpoint_id: Some(_),
                } => {
                    return SignalPlan {
                        source: 1,
                        amplitude: 10.0_f32.powf(db / 20.0),
                    };
                }
                Block::Tone { enabled: true } => {
                    return SignalPlan {
                        source: 2,
                        amplitude: 10.0_f32.powf(db / 20.0),
                    };
                }
                _ => return SignalPlan::default(),
            }
            current = node.id;
        }
        SignalPlan::default()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn external_node_roundtrips_and_only_runs_in_a_complete_valid_route() {
        let mut g = graph();
        g.nodes.push(Node {
            id: id(4),
            block: Block::Plugin {
                source: false,
                consumer: false,
                plugin_id: "test.effect".into(),
                parameters: [(7, 0.25)].into(),
                bypass: false,
            },
        });
        g.edges[1].to = id(4);
        g.edges.push(Edge {
            from_port: 0,
            to_port: 0,
            from: id(4),
            to: id(3),
        });
        g.validate().unwrap();
        let value = serde_json::to_value(&g).unwrap();
        let restored: Graph = serde_json::from_value(value.clone()).unwrap();
        assert_eq!(restored, g);
        let plan = g.render_plan(false);
        assert_eq!(plan.external[0], Some(id(4)));
        assert_eq!(plan.count, 2);
        plan.validate().unwrap();
        let mut malformed = value;
        malformed["nodes"][3]["block"]["parameters"] = serde_json::json!({"07":0.25});
        assert!(serde_json::from_value::<Graph>(malformed).is_err());
        g.edges.pop();
        assert_eq!(g.render_plan(false).external, [None; MAX_PLUGIN_NODES]);
        assert_eq!(g.render_plan(false).signal.source, 0);
        g.edges = vec![
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(1),
                to: id(4),
            },
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(4),
                to: id(2),
            },
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(2),
                to: id(3),
            },
        ];
        g.validate().unwrap();
        let p = g.render_plan(false);
        assert_eq!(p.processor_id(p.order[0]), Some(id(4)));
        assert_eq!(p.processor_id(p.order[1]), Some(id(2)));
    }
    fn id(n: u128) -> NodeId {
        NodeId(Uuid::from_u128(n))
    }
    fn graph() -> Graph {
        Graph {
            schema_version: 1,
            nodes: vec![
                Node {
                    id: id(1),
                    block: Block::Tone { enabled: true },
                },
                Node {
                    id: id(2),
                    block: Block::Gain {
                        gain_db: GainDb::try_from(-6.0).expect("gain"),
                        bypass: false,
                    },
                },
                Node {
                    id: id(3),
                    block: Block::Output {
                        endpoint_id: Some(DeviceId("device".into())),
                    },
                },
            ],
            edges: vec![
                Edge {
                    from_port: 0,
                    to_port: 0,
                    from: id(1),
                    to: id(2),
                },
                Edge {
                    from_port: 0,
                    to_port: 0,
                    from: id(2),
                    to: id(3),
                },
            ],
        }
    }
    #[test]
    fn chain_bypass_and_disconnect() {
        let mut g = graph();
        g.validate().expect("valid");
        let p = g.signal(false);
        assert_eq!(p.source, 2);
        assert!((p.amplitude - 10.0_f32.powf(-6.0 / 20.0)).abs() < 1e-6);
        if let Block::Gain { bypass, .. } = &mut g.nodes[1].block {
            *bypass = true;
        }
        assert!(g.signal(false).amplitude > p.amplitude);
        assert_eq!(g.signal(true).source, 0);
        g.edges.pop();
        assert_eq!(g.signal(false).source, 0);
        assert_eq!(SignalPlan::unpack(p.packed()), p);
    }
    #[test]
    fn execution_order_follows_wires_and_mute_preserves_processors() {
        let mut g = graph();
        g.nodes.insert(
            0,
            Node {
                id: id(4),
                block: Block::Gain {
                    gain_db: GainDb::try_from(-3.0).unwrap(),
                    bypass: true,
                },
            },
        );
        g.edges = vec![
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(1),
                to: id(2),
            },
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(2),
                to: id(4),
            },
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(4),
                to: id(3),
            },
        ];
        g.validate().unwrap();
        let plan = g.render_plan(false);
        assert_eq!(plan.count, 2);
        assert_eq!(plan.gains[plan.order[0] as usize].unwrap().id, id(2));
        assert_eq!(plan.gains[plan.order[1] as usize].unwrap().id, id(4));
        assert!(plan.gains[plan.order[1] as usize].unwrap().bypass);
        let muted = g.render_plan(true);
        assert!(muted.same_route(&plan));
        assert_eq!(muted.signal.amplitude, 0.0);
        g.edges.clear();
        let disconnected = g.render_plan(false);
        assert_eq!(disconnected.count, 0);
        assert_eq!(disconnected.gains.iter().flatten().count(), 2);
    }
    #[test]
    fn rejects_invalid_execution_plans_before_publication() {
        let valid = graph().render_plan(false);
        assert!(valid.validate().is_ok());
        let mut bad = valid;
        bad.count = 17;
        assert!(bad.validate().is_err());
        bad = valid;
        bad.order[0] = 8;
        assert!(bad.validate().is_err());
        bad = valid;
        bad.signal.amplitude = f32::NAN;
        assert!(bad.validate().is_err());
        bad = valid;
        bad.signal.source = 0;
        assert!(bad.validate().is_err());
        bad = valid;
        bad.count = 2;
        assert!(bad.validate().is_err());
        bad = valid;
        bad.gains[0].as_mut().unwrap().amplitude = -1.0;
        assert!(bad.validate().is_err());
    }
    #[test]
    fn rejects_cycles_fan_in_and_wrong_ports() {
        let mut g = graph();
        g.edges.push(Edge {
            from_port: 0,
            to_port: 0,
            from: id(1),
            to: id(3),
        });
        assert!(g.validate().is_err());
        g = graph();
        g.edges = vec![Edge {
            from_port: 0,
            to_port: 0,
            from: id(3),
            to: id(1),
        }];
        assert!(g.validate().is_err());
        g = graph();
        g.nodes.push(Node {
            id: id(4),
            block: Block::Gain {
                gain_db: GainDb::default(),
                bypass: false,
            },
        });
        g.edges = vec![
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(2),
                to: id(4),
            },
            Edge {
                from_port: 0,
                to_port: 0,
                from: id(4),
                to: id(2),
            },
        ];
        assert!(g.validate().is_err());
    }
}
