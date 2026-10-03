//! Persistent document: audio intent and presentation, never live handles or meters.
use crate::{
    ApiError, MAX_PROJECT_BYTES,
    graph::{Graph, NodeId},
};
use serde::{Deserialize, Serialize};

#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Project {
    pub format: String,
    pub version: u32,
    pub graph: Graph,
    pub positions: Vec<Position>,
    pub camera: Camera,
    pub resources: Vec<Resource>,
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub captures: Vec<crate::capture::CaptureResource>,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Position {
    pub node_id: NodeId,
    pub x: f64,
    pub y: f64,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Camera {
    pub x: f64,
    pub y: f64,
    pub scale: f64,
}
#[derive(Serialize, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Resource {
    pub node_id: NodeId,
    pub path: String,
}

impl Project {
    pub fn parse(contents: &str) -> Result<Self, ApiError> {
        if contents.len() > MAX_PROJECT_BYTES {
            return Err(ApiError::invalid("Projeto excede 64 KiB."));
        }
        let mut project: Self = serde_json::from_value(crate::json::parse(contents.as_bytes())?)
            .map_err(|e| ApiError::invalid(e.to_string()))?;
        if project.format != "nodivu-project" || project.version != 1 {
            return Err(ApiError::invalid(
                "Formato ou versão de projeto não suportado.",
            ));
        }
        project.graph.validate()?;
        project.graph.schema_version = 2;
        let coordinate = |v: f64| v.is_finite() && v.abs() <= 10_000_000.0;
        if !coordinate(project.camera.x)
            || !coordinate(project.camera.y)
            || !(0.1..=2.0).contains(&project.camera.scale)
        {
            return Err(ApiError::invalid("Câmera inválida."));
        }
        if project.positions.len() != project.graph.nodes.len() {
            return Err(ApiError::invalid("Cada bloco precisa de uma posição."));
        }
        let mut seen = std::collections::BTreeSet::new();
        for p in &project.positions {
            project.graph.node(p.node_id)?;
            if !seen.insert(p.node_id.0) || !coordinate(p.x) || !coordinate(p.y) {
                return Err(ApiError::invalid("Posição inválida ou duplicada."));
            }
        }
        seen.clear();
        for r in &project.resources {
            if !matches!(
                project.graph.node(r.node_id)?.block,
                crate::graph::Block::Plugin { .. }
            ) || !seen.insert(r.node_id.0)
                || r.path.is_empty()
                || r.path.len() > 4096
                || r.path.contains('\0')
            {
                return Err(ApiError::invalid("Referência de recurso inválida."));
            }
        }
        seen.clear();
        for source in &project.captures {
            if !matches!(
                project.graph.node(source.node_id)?.block,
                crate::graph::Block::Plugin { .. }
            ) || !seen.insert(source.node_id.0)
            {
                return Err(ApiError::invalid(
                    "Referência de captura inválida ou duplicada.",
                ));
            }
            source.selection.validate()?;
        }
        Ok(project)
    }
}
