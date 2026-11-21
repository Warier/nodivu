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
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub slot: Option<u8>,
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
        let mut resource_keys = std::collections::BTreeSet::new();
        for r in &project.resources {
            if !matches!(
                project.graph.node(r.node_id)?.block,
                crate::graph::Block::Plugin { .. }
            ) || !resource_keys.insert((r.node_id.0, r.slot))
                || r.slot.is_some_and(|slot| slot >= 10)
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

#[cfg(test)]
mod tests {
    use super::Project;
    use serde_json::json;
    #[test]
    fn bank_resources_keep_slot_identity_and_legacy_shape() {
        let id = "00000000-0000-4000-8000-000000000001";
        let mut value = json!({"format":"nodivu-project","version":1,"graph":{"schema_version":2,"nodes":[{"id":id,"block":{"kind":"plugin","plugin_id":"org.nodivu.soundboard","source":true,"bypass":false,"parameters":{}}}],"edges":[]},"positions":[{"node_id":id,"x":0,"y":0}],"camera":{"x":0,"y":0,"scale":1},"resources":[{"node_id":id,"slot":0,"path":"one.wav"},{"node_id":id,"slot":9,"path":"two.wav"}]});
        let project = Project::parse(&value.to_string()).unwrap();
        assert_eq!(project.resources[1].slot, Some(9));
        value["resources"][1]["slot"] = json!(0);
        assert!(Project::parse(&value.to_string()).is_err());
        value["resources"][1]["slot"] = json!(10);
        assert!(Project::parse(&value.to_string()).is_err());
        value["resources"] = json!([{"node_id":id,"path":"legacy.wav"}]);
        let project = Project::parse(&value.to_string()).unwrap();
        assert_eq!(project.resources[0].slot, None);
        assert!(
            serde_json::to_value(project).unwrap()["resources"][0]
                .get("slot")
                .is_none()
        );
    }
}
