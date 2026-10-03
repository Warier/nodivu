//! Local audition: one bounded SPSC queue, independent of the primary device clock.
//! Only the render worker writes samples; only the monitor worker advances read.
use crate::audio::Shared;
use nodivu_core::{ApiError, DeviceId, ErrorCode};
use serde_json::{Value, json};
use std::sync::{
    Arc,
    atomic::{AtomicBool, AtomicU64, AtomicUsize, Ordering},
};
use std::thread::JoinHandle;
const CAPACITY: usize = 4096;
pub(crate) const TARGET: usize = 960; // 20 ms at 48 kHz; monitor only.

pub(crate) struct Queue {
    pub enabled: AtomicBool,
    samples: Box<[AtomicU64]>,
    write: AtomicUsize,
    read: AtomicUsize,
    pub dropped: AtomicU64,
    pub underruns: AtomicU64,
}
impl Default for Queue {
    fn default() -> Self {
        Self {
            enabled: AtomicBool::new(false),
            samples: (0..CAPACITY).map(|_| AtomicU64::new(0)).collect(),
            write: AtomicUsize::new(0),
            read: AtomicUsize::new(0),
            dropped: AtomicU64::new(0),
            underruns: AtomicU64::new(0),
        }
    }
}
impl Queue {
    pub fn push(&self, frames: &[[f32; 2]]) {
        if !self.enabled.load(Ordering::Acquire) {
            return;
        }
        let w = self.write.load(Ordering::Relaxed);
        let room = CAPACITY
            - w.wrapping_sub(self.read.load(Ordering::Acquire))
                .min(CAPACITY);
        let n = frames.len().min(room);
        for (i, f) in frames[..n].iter().enumerate() {
            self.samples[w.wrapping_add(i) % CAPACITY].store(
                u64::from(f[0].to_bits()) | (u64::from(f[1].to_bits()) << 32),
                Ordering::Relaxed,
            );
        }
        self.write.store(w.wrapping_add(n), Ordering::Release);
        self.dropped
            .fetch_add((frames.len() - n) as u64, Ordering::Relaxed);
    }
    pub fn available(&self) -> usize {
        self.write
            .load(Ordering::Acquire)
            .wrapping_sub(self.read.load(Ordering::Relaxed))
            .min(CAPACITY)
    }
    // Consumer only. Also used to discard stale audio after a stopped/slow device.
    pub fn trim(&self, keep: usize) {
        let n = self.available().saturating_sub(keep);
        self.read.fetch_add(n, Ordering::Release);
        self.dropped.fetch_add(n as u64, Ordering::Relaxed);
    }
    pub fn pop(&self) -> Option<[f32; 2]> {
        let r = self.read.load(Ordering::Relaxed);
        if r == self.write.load(Ordering::Acquire) {
            return None;
        }
        let v = self.samples[r % CAPACITY].load(Ordering::Relaxed);
        self.read.store(r.wrapping_add(1), Ordering::Release);
        Some([f32::from_bits(v as u32), f32::from_bits((v >> 32) as u32)])
    }
}
pub(crate) struct Monitor {
    shared: Arc<Shared>,
    primary: Arc<Shared>,
    worker: Option<JoinHandle<()>>,
}
impl Monitor {
    #[cfg(windows)]
    pub fn start(primary: Arc<Shared>, endpoint: DeviceId) -> Result<Self, ApiError> {
        let shared = Arc::new(Shared::default());
        let control = shared.clone();
        let source = primary.clone();
        let worker = std::thread::Builder::new()
            .name("wasapi-local-monitor".into())
            .spawn(move || {
                let result = crate::windows_audio::run_monitor(&endpoint, &source, &control);
                source.monitor.enabled.store(false, Ordering::Release);
                if let Err(e) = result {
                    control.failure.store(e.code().0, Ordering::Relaxed);
                    control.state.store(2, Ordering::Release);
                } else {
                    control.state.store(3, Ordering::Release);
                }
            })
            .map_err(|e| {
                ApiError::new(ErrorCode::BackendError, format!("Abrir escuta local: {e}"))
            })?;
        Ok(Self {
            shared,
            primary,
            worker: Some(worker),
        })
    }
    #[cfg(not(windows))]
    pub fn start(_primary: Arc<Shared>, _endpoint: DeviceId) -> Result<Self, ApiError> {
        Err(ApiError::new(
            ErrorCode::BackendError,
            "Escuta local requer Windows.",
        ))
    }
    pub fn snapshot(&self) -> Value {
        let status = self.shared.state.load(Ordering::Acquire);
        let error = if status == 2 {
            Some(crate::audio::audio_error(
                self.shared.failure.load(Ordering::Relaxed),
                2,
            ))
        } else {
            None
        };
        json!({"state":match status{0=>"starting",1=>"running",2=>"error",_=>"stopped"},"error":error,"queued_frames":self.primary.monitor.available(),"dropped_frames":self.primary.monitor.dropped.load(Ordering::Relaxed),"underruns":self.primary.monitor.underruns.load(Ordering::Relaxed),"target_ms":20})
    }
}
impl Drop for Monitor {
    fn drop(&mut self) {
        self.primary.monitor.enabled.store(false, Ordering::Release);
        self.shared.stop.store(true, Ordering::Release);
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bounded_order_and_disable() {
        let q = Queue::default();
        q.push(&[[1., 2.]]);
        assert_eq!(q.available(), 0);
        q.enabled.store(true, Ordering::Release);
        for i in 0..CAPACITY + 7 {
            q.push(&[[i as f32, -(i as f32)]]);
        }
        assert_eq!(q.available(), CAPACITY);
        assert_eq!(q.dropped.load(Ordering::Relaxed), 7);
        for i in 0..CAPACITY {
            assert_eq!(q.pop(), Some([i as f32, -(i as f32)]));
        }
        assert_eq!(q.pop(), None);
        q.push(&[[5., 6.]; 20]);
        q.trim(3);
        assert_eq!(q.available(), 3);
        assert_eq!(q.pop(), Some([5., 6.]));
    }
    #[test]
    fn concurrent_transfer_preserves_stereo_frames() {
        let q = Arc::new(Queue::default());
        q.enabled.store(true, Ordering::Release);
        let source = q.clone();
        let producer = std::thread::spawn(move || {
            for i in 0..10000 {
                while source.available() == CAPACITY {
                    std::thread::yield_now();
                }
                source.push(&[[i as f32, -(i as f32)]]);
            }
        });
        for i in 0..10000 {
            let frame = loop {
                if let Some(f) = q.pop() {
                    break f;
                }
                std::thread::yield_now();
            };
            assert_eq!(frame, [i as f32, -(i as f32)]);
        }
        producer.join().unwrap();
    }
}
