//! Coherent control-to-audio settings; one writer, bounded reads, no allocation in DSP.
use nodivu_core::graph::NodeId;
use std::sync::atomic::{AtomicU64, Ordering::SeqCst};

#[derive(Clone, Copy, Default)]
pub(crate) struct InstanceState {
    pub(crate) id: Option<NodeId>,
    pub(crate) values: [f64; 32],
    pub(crate) bypass: bool,
}
#[derive(Default)]
pub(crate) struct InstanceMailbox {
    sequence: AtomicU64,
    high: AtomicU64,
    low: AtomicU64,
    flags: AtomicU64,
    values: [AtomicU64; 32],
}
impl InstanceMailbox {
    pub(crate) fn publish(&self, state: InstanceState) {
        self.sequence.fetch_add(1, SeqCst);
        let id = state.id.map_or(0, |id| id.0.as_u128());
        self.high.store((id >> 64) as u64, SeqCst);
        self.low.store(id as u64, SeqCst);
        self.flags.store(
            u64::from(state.id.is_some()) | (u64::from(state.bypass) << 1),
            SeqCst,
        );
        for (target, value) in self.values.iter().zip(state.values) {
            target.store(value.to_bits(), SeqCst);
        }
        self.sequence.fetch_add(1, SeqCst);
    }
    pub(crate) fn read_for(&self, requested: NodeId) -> Option<InstanceState> {
        let before = self.sequence.load(SeqCst);
        if before & 1 != 0 {
            return None;
        }
        let id = NodeId(uuid::Uuid::from_u128(
            (u128::from(self.high.load(SeqCst)) << 64) | u128::from(self.low.load(SeqCst)),
        ));
        let flags = self.flags.load(SeqCst);
        if flags & 1 == 0 || id != requested {
            return None;
        }
        let state = InstanceState {
            id: Some(id),
            bypass: flags & 2 != 0,
            values: std::array::from_fn(|i| f64::from_bits(self.values[i].load(SeqCst))),
        };
        (self.sequence.load(SeqCst) == before).then_some(state)
    }
}
/// Try the previous UUID/slot association first, but always reread coherent
/// settings. On a concurrent publication or relocation, scan before falling
/// back to prior settings for this exact UUID, matching the old linear path.
/// Caller owns both arrays, with identical length, for the worker's lifetime.
pub(crate) fn resolve_instance(
    slots: &[InstanceMailbox],
    cached: &[InstanceState],
    id: NodeId,
) -> Option<(usize, InstanceState)> {
    let previous = cached
        .iter()
        .enumerate()
        .find(|(_, state)| state.id == Some(id));
    if let Some((index, _)) = previous
        && let Some(state) = slots.get(index).and_then(|slot| slot.read_for(id))
    {
        return Some((index, state));
    }
    slots
        .iter()
        .enumerate()
        .find_map(|(index, slot)| slot.read_for(id).map(|state| (index, state)))
        .or_else(|| {
            previous
                .filter(|(index, _)| *index < slots.len())
                .map(|(index, state)| (index, *state))
        })
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::Arc;
    #[test]
    fn cached_lookup_refreshes_values_and_follows_uuid_relocation() {
        let slots: Vec<_> = (0..4).map(|_| InstanceMailbox::default()).collect();
        let id = NodeId(uuid::Uuid::from_u128(10));
        let other = NodeId(uuid::Uuid::from_u128(11));
        let old = InstanceState {
            id: Some(id),
            values: [1.0; 32],
            bypass: false,
        };
        let mut cached = vec![InstanceState::default(); 4];
        cached[3] = old;
        slots[3].publish(InstanceState {
            values: [2.0; 32],
            bypass: true,
            ..old
        });
        let (index, fresh) = resolve_instance(&slots, &cached, id).unwrap();
        assert_eq!(index, 3);
        assert_eq!(fresh.values, [2.0; 32]);
        assert!(fresh.bypass);
        // project.open can reassign the same UUID to a different physical slot.
        slots[3].publish(InstanceState {
            id: Some(other),
            ..old
        });
        slots[1].publish(InstanceState {
            values: [3.0; 32],
            ..old
        });
        let (index, moved) = resolve_instance(&slots, &cached, id).unwrap();
        assert_eq!(index, 1);
        assert_eq!(moved.values, [3.0; 32]);
        assert!(resolve_instance(&slots, &cached, NodeId(uuid::Uuid::from_u128(12))).is_none());
    }
    #[test]
    fn overlapping_publication_uses_only_the_same_uuids_previous_state() {
        let slot = InstanceMailbox::default();
        let id = NodeId(uuid::Uuid::from_u128(10));
        let previous = InstanceState {
            id: Some(id),
            values: [5.0; 32],
            bypass: false,
        };
        slot.publish(previous);
        slot.sequence.store(3, SeqCst);
        let slots = [slot];
        let (_, state) = resolve_instance(&slots, &[previous], id).unwrap();
        assert_eq!(state.values, [5.0; 32]);
        assert!(resolve_instance(&slots, &[previous], NodeId(uuid::Uuid::from_u128(11))).is_none());
        assert!(resolve_instance(&slots, &[InstanceState::default()], id).is_none());
    }
    #[test]
    fn concurrent_settings_never_mix_uuid_bypass_or_parameter_batch() {
        let mailbox = Arc::new(InstanceMailbox::default());
        let writer = mailbox.clone();
        let id = NodeId(uuid::Uuid::from_u128(7));
        let thread = std::thread::spawn(move || {
            for i in 1..10_000 {
                writer.publish(InstanceState {
                    id: Some(id),
                    values: [f64::from(i); 32],
                    bypass: i % 2 == 0,
                });
            }
        });
        while !thread.is_finished() {
            if let Some(state) = mailbox.read_for(id) {
                assert!(state.values.iter().all(|v| *v == state.values[0]));
                assert_eq!(state.bypass, (state.values[0] as u64).is_multiple_of(2));
                assert_eq!(state.id, Some(id));
            }
        }
        thread.join().unwrap();
        assert!(mailbox.read_for(NodeId(uuid::Uuid::from_u128(8))).is_none());
        mailbox.sequence.store(1, SeqCst);
        assert!(mailbox.read_for(id).is_none());
    }
}
