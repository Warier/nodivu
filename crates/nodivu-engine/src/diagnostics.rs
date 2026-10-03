//! CPU por etapa; não confundir duração da chamada com atraso físico/algorítmico.
//! Contadores limitados no worker, publicação ~10 Hz, JSON apenas no controle.
use serde_json::{Value, json};
use std::{
    sync::atomic::{AtomicI32, AtomicU32, AtomicU64, Ordering},
    time::Instant,
};

pub(crate) const STAGES: usize = 6;
pub(crate) const NAMES: [&str; STAGES] = [
    "capture_packet",
    "source_or_resampler",
    "interleaved_to_planar",
    "effects_and_monitor",
    "protection_and_interleave",
    "render_packet_total",
];

#[derive(Default)]
pub(crate) struct DevicePeriods {
    pub minimum_requested: AtomicU32,
    pub status: AtomicU32, // 0 não consultado, 1 válido, 2 erro.
    pub error: AtomicI32,
    pub rate: AtomicU32,
    pub default_frames: AtomicU32,
    pub fundamental_frames: AtomicU32,
    pub min_frames: AtomicU32,
    pub max_frames: AtomicU32,
}
impl DevicePeriods {
    pub(crate) fn snapshot(&self) -> Value {
        let status = self.status.load(Ordering::Acquire);
        if status != 1 {
            return json!({"status":if status==0 {"not_queried"} else {"error"}, "hresult":if status==2 {Some(format!("0x{:08X}",self.error.load(Ordering::Relaxed) as u32))} else {None}});
        }
        let rate = self.rate.load(Ordering::Relaxed);
        json!({"status":"available", "queried_mix_rate_hz":rate,
            "default_frames":self.default_frames.load(Ordering::Relaxed),
            "fundamental_frames":self.fundamental_frames.load(Ordering::Relaxed),
            "min_frames":self.min_frames.load(Ordering::Relaxed),
            "max_frames":self.max_frames.load(Ordering::Relaxed),
            "min_ms":f64::from(self.min_frames.load(Ordering::Relaxed))*1000.0/f64::from(rate),
            "requested_minimum":self.minimum_requested.load(Ordering::Relaxed)!=0})
    }
}

#[derive(Default, Clone, Copy)]
pub struct Timing {
    pub calls: u64,
    pub total_ns: u64,
    pub max_ns: u64,
    pub last_ns: u64,
    pub over_frame_budget: u64,
}
impl Timing {
    pub fn record(&mut self, elapsed_ns: u64, frames: usize, sample_rate: u32) {
        self.calls = self.calls.saturating_add(1);
        self.total_ns = self.total_ns.saturating_add(elapsed_ns);
        self.last_ns = elapsed_ns;
        self.max_ns = self.max_ns.max(elapsed_ns);
        if sample_rate > 0
            && u128::from(elapsed_ns) * u128::from(sample_rate) > frames as u128 * 1_000_000_000
        {
            self.over_frame_budget = self.over_frame_budget.saturating_add(1);
        }
    }
    pub(crate) fn finish(&mut self, start: Instant, frames: usize, sample_rate: u32) {
        self.record(
            start.elapsed().as_nanos().min(u128::from(u64::MAX)) as u64,
            frames,
            sample_rate,
        );
    }
}

#[derive(Default)]
pub(crate) struct SharedTiming {
    calls: AtomicU64,
    total_ns: AtomicU64,
    max_ns: AtomicU64,
    last_ns: AtomicU64,
    over_frame_budget: AtomicU64,
}
impl SharedTiming {
    pub(crate) fn publish(&self, timing: &Timing) {
        self.total_ns.store(timing.total_ns, Ordering::SeqCst);
        self.max_ns.store(timing.max_ns, Ordering::SeqCst);
        self.last_ns.store(timing.last_ns, Ordering::SeqCst);
        self.over_frame_budget
            .store(timing.over_frame_budget, Ordering::SeqCst);
        self.calls.store(timing.calls, Ordering::SeqCst);
    }
    pub(crate) fn snapshot(&self) -> Value {
        // Diagnóstico aproximado: campos podem pertencer a publicações adjacentes.
        // Não usar o snapshot para controle de buffers ou decisões de áudio.
        let calls = self.calls.load(Ordering::SeqCst);
        json!({"calls": calls,
            "mean_us": if calls == 0 { 0.0 } else { self.total_ns.load(Ordering::SeqCst) as f64 / calls as f64 / 1000.0 },
            "max_us": self.max_ns.load(Ordering::SeqCst) as f64 / 1000.0,
            "last_us": self.last_ns.load(Ordering::SeqCst) as f64 / 1000.0,
            "over_frame_budget": self.over_frame_budget.load(Ordering::SeqCst)})
    }
}

#[derive(Default)]
pub(crate) struct SharedNode {
    sequence: AtomicU64,
    high: AtomicU64,
    low: AtomicU64,
    flags: AtomicU64,
    peak: AtomicU64,
    timing: SharedTiming,
}
impl SharedNode {
    pub(crate) fn publish(&self, slot: &crate::pipeline::Slot, connected: bool, bypass: bool) {
        use std::sync::atomic::Ordering::SeqCst;
        self.sequence.fetch_add(1, SeqCst);
        let id = slot.id.map_or(0, |id| id.0.as_u128());
        self.high.store((id >> 64) as u64, SeqCst);
        self.low.store(id as u64, SeqCst);
        self.flags.store(
            u64::from(slot.id.is_some()) | (u64::from(connected) << 1) | (u64::from(bypass) << 2),
            SeqCst,
        );
        self.peak.store(u64::from(slot.peak.to_bits()), SeqCst);
        self.timing.publish(&slot.timing);
        self.sequence.fetch_add(1, SeqCst);
    }
    pub(crate) fn snapshot(&self) -> Option<Value> {
        use std::sync::atomic::Ordering::SeqCst;
        let before = self.sequence.load(SeqCst);
        if before & 1 != 0 {
            return None;
        }
        let flags = self.flags.load(SeqCst);
        if flags & 1 == 0 {
            return None;
        }
        let id = (u128::from(self.high.load(SeqCst)) << 64) | u128::from(self.low.load(SeqCst));
        let peak = f32::from_bits(self.peak.load(SeqCst) as u32);
        let timing = self.timing.snapshot();
        if before != self.sequence.load(SeqCst) {
            return None;
        }
        Some(
            json!({"node_id": uuid::Uuid::from_u128(id), "plugin_id":"org.nodivu.gain", "connected":flags & 2 != 0,
            "bypass":flags & 4 != 0, "output_peak_before_monitor":peak, "cpu":timing, "declared_latency_frames":0}),
        )
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn measures_cpu_budget_without_claiming_signal_latency() {
        let mut t = Timing::default();
        t.record(1_000_000, 48, 48_000);
        t.record(1_000_001, 48, 48_000);
        assert_eq!(t.calls, 2);
        assert_eq!(t.over_frame_budget, 1);
        assert_eq!(t.total_ns, 2_000_001);
        assert_eq!(t.max_ns, 1_000_001);
        let shared = SharedTiming::default();
        shared.publish(&t);
        assert_eq!(shared.snapshot()["calls"], 2);
    }
}
#[derive(Default)]
pub(crate) struct SharedRouteNode {
    sequence: AtomicU64,
    high: AtomicU64,
    low: AtomicU64,
    present: AtomicU64,
    peak: AtomicU64,
    calls: AtomicU64,
}
impl SharedRouteNode {
    pub fn publish(&self, id: Option<nodivu_core::graph::NodeId>, peak: f32, calls: u64) {
        use std::sync::atomic::Ordering::SeqCst;
        self.sequence.fetch_add(1, SeqCst);
        let bits = id.map_or(0, |id| id.0.as_u128());
        self.high.store((bits >> 64) as u64, SeqCst);
        self.low.store(bits as u64, SeqCst);
        self.present.store(u64::from(id.is_some()), SeqCst);
        self.peak.store(u64::from(peak.to_bits()), SeqCst);
        self.calls.store(calls, SeqCst);
        self.sequence.fetch_add(1, SeqCst);
    }
    pub fn snapshot(&self, active: bool) -> Option<Value> {
        use std::sync::atomic::Ordering::SeqCst;
        let before = self.sequence.load(SeqCst);
        if before & 1 != 0 || self.present.load(SeqCst) == 0 {
            return None;
        }
        let id = uuid::Uuid::from_u128(
            (u128::from(self.high.load(SeqCst)) << 64) | u128::from(self.low.load(SeqCst)),
        );
        let peak = if active {
            f32::from_bits(self.peak.load(SeqCst) as u32)
        } else {
            0.0
        };
        let calls = self.calls.load(SeqCst);
        if before != self.sequence.load(SeqCst) {
            return None;
        }
        Some(json!({"node_id":id,"peak":peak,"calls":calls}))
    }
}
