//! Control lookup microbenchmark, not an audio latency/CPU certification.
//! cargo run --release --locked -p nodivu-app-backend --example profile_plugin_lookup
#[cfg(all(windows, not(test)))]
#[path = "../src/instance_mailbox.rs"]
mod instance_mailbox;

#[cfg(all(windows, not(test)))]
fn main() {
    use instance_mailbox::{InstanceMailbox, InstanceState, resolve_instance};
    use nodivu_core::graph::NodeId;
    use std::{hint::black_box, time::Instant};
    // Original lookup for a reproducible before/after using identical inputs.
    fn linear(
        slots: &[InstanceMailbox],
        cached: &[InstanceState],
        id: NodeId,
    ) -> Option<(usize, InstanceState)> {
        slots
            .iter()
            .enumerate()
            .find_map(|(i, s)| s.read_for(id).map(|v| (i, v)))
            .or_else(|| {
                cached
                    .iter()
                    .enumerate()
                    .find(|(_, s)| s.id == Some(id))
                    .map(|(i, s)| (i, *s))
            })
    }
    type Lookup =
        fn(&[InstanceMailbox], &[InstanceState], NodeId) -> Option<(usize, InstanceState)>;
    for (name, count, first, active) in [
        ("small", 8, 0, 1),
        ("full-early", 64, 0, 8),
        ("full-late", 64, 56, 8),
    ] {
        let slots: Vec<_> = (0..count).map(|_| InstanceMailbox::default()).collect();
        let mut cached = vec![InstanceState::default(); count];
        let ids: Vec<_> = (first..first + active)
            .map(|i| NodeId(uuid::Uuid::from_u128(i as u128 + 1)))
            .collect();
        for (offset, id) in ids.iter().enumerate() {
            let state = InstanceState {
                id: Some(*id),
                values: [0.5; 32],
                bypass: false,
            };
            slots[first + offset].publish(state);
            cached[first + offset] = state;
        }
        for (label, lookup) in [
            ("linear", linear as Lookup),
            ("cached", resolve_instance as Lookup),
        ] {
            let mut samples = Vec::new();
            for batch in 0..110 {
                let start = Instant::now();
                for i in 0..1000 {
                    let (_, state) = lookup(
                        black_box(&slots),
                        black_box(&cached),
                        black_box(ids[i % ids.len()]),
                    )
                    .expect("fixture UUID");
                    black_box(state);
                }
                if batch >= 10 {
                    samples.push(start.elapsed().as_nanos() as f64 / 1000.0);
                }
            }
            samples.sort_by(f64::total_cmp);
            println!(
                "{}",
                serde_json::json!({"case":name,"algorithm":label,"slots":count,"active":active,
                "metric":"batch_mean_ns_per_lookup","batch_lookups":1000,"samples":100,
                "p50":samples[49],"p95":samples[94],"p99":samples[98]})
            );
        }
    }
}
#[cfg(any(not(windows), test))]
fn main() {}
