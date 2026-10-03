//! Caixa de plano com um escritor e snapshot de uma tentativa, sem heap/locks/espera.
//! Todos os campos são atômicos SeqCst: leitura só aceita duas sequências iguais/pares.
//! Isso evita data race de seqlocks baseados em memória não atômica e leitura parcial.
use nodivu_core::graph::{
    GainStep, MAX_GAIN_NODES, MAX_PLUGIN_NODES, NodeId, RenderPlan, SignalPlan,
};
use std::sync::atomic::{AtomicU64, Ordering::SeqCst};
use uuid::Uuid;

#[derive(Default)]
struct AtomicGain {
    high: AtomicU64,
    low: AtomicU64,
    value: AtomicU64,
}
#[derive(Default)]
pub(crate) struct PlanMailbox {
    sequence: AtomicU64,
    signal: AtomicU64,
    order: AtomicU64,
    count: AtomicU64,
    gains: [AtomicGain; MAX_GAIN_NODES],
    external: [AtomicGain; MAX_PLUGIN_NODES],
    live: AtomicU64,
    routing: AtomicRouting,
}
struct AtomicRouting([AtomicU64; nodivu_core::live_routing::WORDS]);
impl Default for AtomicRouting {
    fn default() -> Self {
        Self(std::array::from_fn(|_| AtomicU64::new(0)))
    }
}
impl PlanMailbox {
    /// Chamadas serializadas pelo &mut AudioSession; antes do spawn há um só owner.
    pub(crate) fn publish(&self, plan: RenderPlan) {
        self.sequence.fetch_add(1, SeqCst);
        self.live.store(u64::from(plan.live.is_some()), SeqCst);
        if let Some(live) = plan.live {
            for (a, v) in self.routing.0.iter().zip(live.words()) {
                a.store(v, SeqCst);
            }
        }
        self.signal.store(plan.signal.packed(), SeqCst);
        for (target, source) in self.external.iter().zip(plan.external) {
            let id = source.map_or(0, |id| id.0.as_u128());
            target.high.store((id >> 64) as u64, SeqCst);
            target.low.store(id as u64, SeqCst);
            target.value.store(u64::from(source.is_some()), SeqCst);
        }
        self.count.store(u64::from(plan.count), SeqCst);
        let mut order = 0;
        for (i, slot) in plan.order.iter().enumerate() {
            order |= u64::from(*slot) << (i * 4);
        }
        self.order.store(order, SeqCst);
        for (target, gain) in self.gains.iter().zip(plan.gains) {
            if let Some(gain) = gain {
                let id = gain.id.0.as_u128();
                target.high.store((id >> 64) as u64, SeqCst);
                target.low.store(id as u64, SeqCst);
                target.value.store(
                    u64::from(gain.amplitude.to_bits())
                        | (1 << 32)
                        | (u64::from(gain.bypass) << 33),
                    SeqCst,
                );
            } else {
                target.value.store(0, SeqCst);
            }
        }
        self.sequence.fetch_add(1, SeqCst);
    }
    pub(crate) fn read(&self) -> Option<RenderPlan> {
        let before = self.sequence.load(SeqCst);
        if before & 1 != 0 {
            return None;
        }
        let mut plan = RenderPlan {
            signal: SignalPlan::unpack(self.signal.load(SeqCst)),
            ..RenderPlan::default()
        };
        if self.live.load(SeqCst) != 0 {
            let words = std::array::from_fn(|i| self.routing.0[i].load(SeqCst));
            plan.live = Some(nodivu_core::live_routing::LivePlan::from_words(&words));
        }
        let order = self.order.load(SeqCst);
        for (target, source) in plan.external.iter_mut().zip(&self.external) {
            let high = source.high.load(SeqCst);
            let low = source.low.load(SeqCst);
            if source.value.load(SeqCst) != 0 {
                *target = Some(NodeId(Uuid::from_u128(
                    (u128::from(high) << 64) | u128::from(low),
                )));
            }
        }
        plan.count = self.count.load(SeqCst) as u8;
        for (i, slot) in plan.order.iter_mut().enumerate() {
            *slot = ((order >> (i * 4)) & 15) as u8;
        }
        for (gain, source) in plan.gains.iter_mut().zip(&self.gains) {
            let high = source.high.load(SeqCst);
            let low = source.low.load(SeqCst);
            let value = source.value.load(SeqCst);
            if value & (1 << 32) != 0 {
                *gain = Some(GainStep {
                    id: NodeId(Uuid::from_u128((u128::from(high) << 64) | u128::from(low))),
                    amplitude: f32::from_bits(value as u32),
                    bypass: value & (1 << 33) != 0,
                });
            }
        }
        if before != self.sequence.load(SeqCst) {
            return None;
        }
        Some(plan)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn overlapping_publication_never_returns_torn_plan() {
        let mailbox = std::sync::Arc::new(PlanMailbox::default());
        let writer = mailbox.clone();
        let thread = std::thread::spawn(move || {
            for i in 1..20_000_u128 {
                let mut plan = RenderPlan {
                    count: 1,
                    signal: SignalPlan {
                        source: 2,
                        amplitude: i as f32,
                    },
                    ..RenderPlan::default()
                };
                plan.gains[0] = Some(GainStep {
                    id: NodeId(Uuid::from_u128((i << 64) | i)),
                    amplitude: i as f32,
                    bypass: i % 2 == 0,
                });
                plan.external[0] = (i % 2 == 0).then_some(NodeId(Uuid::from_u128((i << 64) | i)));
                plan.external[7] = Some(NodeId(Uuid::from_u128(i)));
                plan.order[15] = (i % 16) as u8;
                let mut live = nodivu_core::live_routing::LivePlan {
                    count: 1,
                    delay_frames: i as u32,
                    ..Default::default()
                };
                live.steps[0].id = plan.gains[0].map(|g| g.id);
                live.steps[0].kind = 1;
                live.steps[0].enabled = i % 2 == 0;
                plan.live = Some(live);
                writer.publish(plan);
            }
        });
        while !thread.is_finished() {
            if let Some(plan) = mailbox.read()
                && let Some(gain) = plan.gains[0]
            {
                let id = gain.id.0.as_u128();
                assert_eq!(id >> 64, id & u128::from(u64::MAX));
                assert_eq!(gain.amplitude, plan.signal.amplitude);
                assert_eq!(gain.amplitude, (id as u64) as f32);
                assert_eq!(gain.bypass, id % 2 == 0);
                assert_eq!(plan.external[0], gain.bypass.then_some(gain.id));
                assert_eq!(
                    plan.external[7],
                    Some(NodeId(Uuid::from_u128(id & u128::from(u64::MAX))))
                );
                assert_eq!(plan.order[15], (id % 16) as u8);
                let live = plan.live.unwrap();
                assert_eq!(live.steps[0].id, Some(gain.id));
                assert_eq!(live.steps[0].enabled, gain.bypass);
                assert_eq!(live.delay_frames, (id as u64) as u32);
            }
        }
        thread.join().unwrap();
        mailbox.sequence.store(1, SeqCst);
        assert!(mailbox.read().is_none()); // Não espera pelo escritor.
    }
}
