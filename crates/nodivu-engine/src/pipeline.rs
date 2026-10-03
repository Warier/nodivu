//! Instâncias fixas por ID; plano/estado sem heap. Válido para os ganhos internos atuais.
//! Um futuro plugin com recursos precisa preparar/recolher esses recursos fora do áudio.
use crate::diagnostics::Timing;
use nodivu_block::{ParameterEvent, PreparedEffect, StereoMut};
use nodivu_core::graph::{MAX_GAIN_NODES, NodeId, RenderPlan};
use nodivu_plugin_gain::{AMPLITUDE, Gain};
use std::time::Instant;

pub(crate) struct Slot {
    pub id: Option<NodeId>,
    pub effect: Gain,
    pub timing: Timing,
    pub peak: f32,
}
impl Default for Slot {
    fn default() -> Self {
        Self {
            id: None,
            effect: Gain::unity(),
            timing: Timing::default(),
            peak: 0.0,
        }
    }
}
#[derive(Default)]
pub(crate) struct Pipeline {
    pub plan: RenderPlan,
    pub slots: [Slot; MAX_GAIN_NODES],
}
impl Pipeline {
    pub fn apply(&mut self, plan: RenderPlan) {
        // Move estado entre slots pelo ID; parâmetros ou ordem do documento não recriam instância.
        let mut previous = std::mem::take(&mut self.slots);
        for (slot, spec) in self.slots.iter_mut().zip(plan.gains) {
            if let Some(spec) = spec {
                if let Some(old) = previous.iter_mut().find(|old| old.id == Some(spec.id)) {
                    *slot = std::mem::take(old);
                } else {
                    slot.id = Some(spec.id);
                }
            }
        }
        self.plan = plan;
    }
    #[cfg(test)]
    pub fn process(&mut self, left: &mut [f32], right: &mut [f32]) -> u64 {
        self.process_with_effect(left, right, &mut |_, _| false)
    }
    pub fn process_with_effect(
        &mut self,
        left: &mut [f32],
        right: &mut [f32],
        effect: &mut crate::external_audio::StereoEffect<'_>,
    ) -> u64 {
        let mut errors = 0;
        for &index in &self.plan.order[..usize::from(self.plan.count)] {
            if usize::from(index) >= MAX_GAIN_NODES {
                if !effect(StereoMut::new(left, right), self.plan.processor_id(index)) {
                    left.fill(0.0);
                    right.fill(0.0);
                    errors += 1;
                }
                continue;
            }
            let Some(spec) = self.plan.gains[usize::from(index)] else {
                continue;
            };
            let slot = &mut self.slots[usize::from(index)];
            let start = Instant::now();
            let events = [ParameterEvent {
                frame_offset: 0,
                id: AMPLITUDE,
                value: if spec.bypass {
                    1.0
                } else {
                    f64::from(spec.amplitude)
                },
            }];
            if slot
                .effect
                .process(StereoMut::new(left, right), &events)
                .is_err()
            {
                left.fill(0.0);
                right.fill(0.0);
                errors += 1;
            }
            slot.timing.finish(start, left.len(), 48_000);
            for sample in left.iter().chain(right.iter()) {
                slot.peak = slot.peak.max(sample.abs());
            }
        }
        errors
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nodivu_core::graph::GainStep;
    use uuid::Uuid;
    #[test]
    fn interleaves_external_nodes_and_multiplies_gain_instances_without_intermediate_clipping() {
        let mut pipeline = Pipeline::default();
        let mut plan = RenderPlan {
            count: 4,
            ..RenderPlan::default()
        };
        for i in 0..2 {
            plan.gains[i] = Some(GainStep {
                id: NodeId(Uuid::from_u128(i as u128 + 1)),
                amplitude: 2.0,
                bypass: false,
            });
            plan.external[i] = Some(NodeId(Uuid::from_u128(i as u128 + 3)));
        }
        plan.order[..4].copy_from_slice(&[8, 0, 9, 1]);
        pipeline.apply(plan);
        let mut visited = Vec::new();
        let mut effect = |audio: StereoMut<'_>, id| {
            visited.push(id);
            for sample in audio.left.iter_mut().chain(audio.right) {
                // Noncommutative effects detect accidental grouping/reordering around gains.
                *sample = if id == plan.external[0] {
                    *sample + 1.0
                } else {
                    *sample * *sample
                };
            }
            true
        };
        let mut left = [0.5; 480];
        let mut right = [0.5; 480];
        pipeline.process_with_effect(&mut left, &mut right, &mut effect);
        left.fill(0.5);
        right.fill(0.5);
        assert_eq!(
            pipeline.process_with_effect(&mut left, &mut right, &mut effect),
            0
        );
        assert_eq!(left, [18.0; 480]); // ((0.5 + 1) * 2)^2 * 2, no clipping in nodes.
        assert_eq!(
            visited,
            [
                plan.external[0],
                plan.external[1],
                plan.external[0],
                plan.external[1]
            ]
        );
        plan.count = 2;
        plan.order[..2].copy_from_slice(&[0, 1]);
        for gain in plan.gains.iter_mut().flatten() {
            gain.amplitude = 0.5;
        }
        pipeline.apply(plan);
        left.fill(1.0);
        right.fill(1.0);
        pipeline.process(&mut left, &mut right);
        left.fill(1.0);
        right.fill(1.0);
        pipeline.process(&mut left, &mut right);
        assert_eq!(left, [0.25; 480]);
    }
    #[test]
    fn moving_slots_preserves_ramps_and_deleted_ids_do_not_leak_state() {
        let mut pipeline = Pipeline::default();
        let a = GainStep {
            id: NodeId(Uuid::from_u128(1)),
            amplitude: 0.0,
            bypass: false,
        };
        let b = GainStep {
            id: NodeId(Uuid::from_u128(2)),
            amplitude: 0.5,
            bypass: false,
        };
        let mut plan = RenderPlan {
            count: 2,
            ..RenderPlan::default()
        };
        plan.gains[0] = Some(a);
        plan.gains[1] = Some(b);
        plan.order[1] = 1;
        pipeline.apply(plan);
        let mut left = [1.0; 64];
        let mut right = [1.0; 64];
        assert_eq!(pipeline.process(&mut left, &mut right), 0);
        let last = left[63];
        // Mesmo caminho com outra ordem de armazenamento: não reiniciar rampa/counters.
        plan.gains.swap(0, 1);
        plan.order.fill(0);
        plan.order[0] = 1;
        pipeline.apply(plan);
        left.fill(1.0);
        right.fill(1.0);
        pipeline.process(&mut left, &mut right);
        assert!(left[0] < last);
        assert_eq!(pipeline.slots[1].timing.calls, 2);
        plan.gains[1] = Some(GainStep {
            id: NodeId(Uuid::from_u128(3)),
            amplitude: 1.0,
            bypass: false,
        });
        plan.count = 1;
        pipeline.apply(plan);
        left.fill(1.0);
        right.fill(1.0);
        pipeline.process(&mut left, &mut right);
        assert_eq!(left, [1.0; 64]);
        assert_eq!(pipeline.slots[1].timing.calls, 1);
    }
    #[test]
    fn bypass_keeps_processing_and_disconnected_nodes_pause() {
        let mut pipeline = Pipeline::default();
        let mut plan = RenderPlan {
            count: 1,
            ..RenderPlan::default()
        };
        plan.gains[0] = Some(GainStep {
            id: NodeId(Uuid::from_u128(1)),
            amplitude: 0.0,
            bypass: true,
        });
        pipeline.apply(plan);
        let mut left = [1.0; 480];
        let mut right = [0.5; 480];
        pipeline.process(&mut left, &mut right);
        assert_eq!(left, [1.0; 480]);
        assert_eq!(right, [0.5; 480]);
        plan.count = 0;
        pipeline.apply(plan);
        pipeline.process(&mut left, &mut right);
        assert_eq!(pipeline.slots[0].timing.calls, 1);
    }
}
