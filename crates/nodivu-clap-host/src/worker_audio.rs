//! Experimental asynchronous effect boundary, independent of CLAP and Python.
//! Construct/drop outside audio. `process`/`reset` never allocate, lock or wait.
//! One audio owner and one service owner; neither endpoint is clonable.
use std::sync::{
    Arc,
    atomic::{AtomicU32, AtomicU64, AtomicUsize, Ordering::*},
};

pub const FRAMES: usize = 480;
pub const LATENCY_FRAMES: u64 = 960; // 20 ms at the fixed 48 kHz bus rate.
const CAPACITY: usize = 4;
pub type Samples = [[f32; FRAMES]; 2];

/// Identity is private: service may change PCM, but cannot relabel its timeline.
pub struct WorkItem {
    epoch: u64,
    start: u64,
    gain: f32,
    pub samples: Samples,
}
impl WorkItem {
    pub fn epoch(&self) -> u64 {
        self.epoch
    }
    pub fn gain(&self) -> f32 {
        self.gain
    }
    pub fn start_frame(&self) -> u64 {
        self.start
    }
}

// Atomic payload keeps this implementation entirely safe Rust. Relaxed payload
// operations are ordered by the single-producer/single-consumer release/acquire
// cursors. Producer never overwrites an occupied slot; consumer never publishes.
struct Slot {
    epoch: AtomicU64,
    start: AtomicU64,
    gain: AtomicU32,
    samples: [[AtomicU32; FRAMES]; 2],
}
struct Ring {
    slots: [Slot; CAPACITY],
    write: AtomicUsize,
    read: AtomicUsize,
}
impl Ring {
    fn new() -> Self {
        Self {
            slots: std::array::from_fn(|_| Slot {
                epoch: AtomicU64::new(0),
                start: AtomicU64::new(0),
                gain: AtomicU32::new(1.0_f32.to_bits()),
                samples: std::array::from_fn(|_| std::array::from_fn(|_| AtomicU32::new(0))),
            }),
            write: AtomicUsize::new(0),
            read: AtomicUsize::new(0),
        }
    }
    fn push(&self, item: &WorkItem) -> bool {
        let write = self.write.load(Relaxed);
        if write.wrapping_sub(self.read.load(Acquire)) >= CAPACITY {
            return false;
        }
        let slot = &self.slots[write % CAPACITY];
        slot.epoch.store(item.epoch, Relaxed);
        slot.start.store(item.start, Relaxed);
        slot.gain.store(item.gain.to_bits(), Relaxed);
        for (to, from) in slot
            .samples
            .iter()
            .flatten()
            .zip(item.samples.iter().flatten())
        {
            to.store(from.to_bits(), Relaxed);
        }
        self.write.store(write.wrapping_add(1), Release);
        true
    }
    fn pop(&self) -> Option<WorkItem> {
        let read = self.read.load(Relaxed);
        if read == self.write.load(Acquire) {
            return None;
        }
        let slot = &self.slots[read % CAPACITY];
        let item = WorkItem {
            epoch: slot.epoch.load(Relaxed),
            start: slot.start.load(Relaxed),
            gain: f32::from_bits(slot.gain.load(Relaxed)),
            samples: std::array::from_fn(|c| {
                std::array::from_fn(|i| f32::from_bits(slot.samples[c][i].load(Relaxed)))
            }),
        };
        self.read.store(read.wrapping_add(1), Release);
        Some(item)
    }
}
struct Shared {
    input: Ring,
    output: Ring,
    epoch: AtomicU64,
    rejected: AtomicU64,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Diagnostics {
    pub submitted: u64,
    pub input_overflows: u64,
    pub missing_blocks: u64,
    pub discarded_responses: u64,
    pub rejected_completions: u64,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum AudioError {
    InvalidFrames,
    InvalidControl,
    NonFiniteInput,
    TimelineExhausted,
}

pub struct AudioBridge {
    shared: Arc<Shared>,
    epoch: u64,
    position: u64,
    input: WorkItem,
    output: Samples,
    pending: Option<WorkItem>,
    diagnostics: Diagnostics,
}
pub struct WorkerPort {
    shared: Arc<Shared>,
}

/// Preparation only. Each pair belongs to exactly one node/instance.
pub fn prepare() -> (AudioBridge, WorkerPort) {
    let shared = Arc::new(Shared {
        input: Ring::new(),
        output: Ring::new(),
        epoch: AtomicU64::new(1),
        rejected: AtomicU64::new(0),
    });
    (
        AudioBridge {
            shared: Arc::clone(&shared),
            epoch: 1,
            position: 0,
            input: WorkItem {
                epoch: 1,
                start: 0,
                gain: 1.0,
                samples: [[0.0; FRAMES]; 2],
            },
            output: [[0.0; FRAMES]; 2],
            pending: None,
            diagnostics: Diagnostics::default(),
        },
        WorkerPort { shared },
    )
}
impl AudioBridge {
    pub fn diagnostics(&self) -> Diagnostics {
        Diagnostics {
            rejected_completions: self.shared.rejected.load(Relaxed),
            ..self.diagnostics
        }
    }
    /// Invalidate outstanding work without resetting producer-owned ring cursors.
    /// Counters are cumulative; finite epoch exhaustion is an explicit failure.
    pub fn reset(&mut self) -> Result<(), AudioError> {
        self.epoch = self
            .epoch
            .checked_add(1)
            .ok_or(AudioError::TimelineExhausted)?;
        self.position = 0;
        self.input.samples = [[0.0; FRAMES]; 2];
        self.output = [[0.0; FRAMES]; 2];
        self.pending = None;
        self.shared.epoch.store(self.epoch, Release);
        Ok(())
    }
    /// Variable callback lengths are packetized into 480-frame worker requests.
    /// Output is decided once at each packet boundary; late audio is never shifted.
    /// Invalid input returns before mutating the caller's buffer or timeline.
    pub fn process(&mut self, samples: &mut Samples, frames: usize) -> Result<(), AudioError> {
        self.process_with_gain(samples, frames, 1.0)
    }
    /// Control is latched with the first sample of each packet, not read later by Python.
    pub fn process_with_gain(
        &mut self,
        samples: &mut Samples,
        frames: usize,
        gain: f32,
    ) -> Result<(), AudioError> {
        if !gain.is_finite() || !(0.0..=4.0).contains(&gain) {
            return Err(AudioError::InvalidControl);
        }
        if !(1..=FRAMES).contains(&frames) {
            return Err(AudioError::InvalidFrames);
        }
        if samples
            .iter()
            .any(|c| c[..frames].iter().any(|x| !x.is_finite()))
        {
            return Err(AudioError::NonFiniteInput);
        }
        if self
            .position
            .checked_add(frames as u64 + LATENCY_FRAMES)
            .is_none()
        {
            return Err(AudioError::TimelineExhausted);
        }
        for i in 0..frames {
            let offset = (self.position % FRAMES as u64) as usize;
            if offset == 0 {
                self.input.gain = gain;
                self.select_output();
            }
            for (c, channel) in samples.iter_mut().enumerate() {
                self.input.samples[c][offset] = channel[i];
                channel[i] = self.output[c][offset];
            }
            self.position += 1;
            if offset + 1 == FRAMES {
                self.input.epoch = self.epoch;
                self.input.start = self.position - FRAMES as u64;
                if self.shared.input.push(&self.input) {
                    self.diagnostics.submitted = self.diagnostics.submitted.saturating_add(1);
                } else {
                    self.diagnostics.input_overflows =
                        self.diagnostics.input_overflows.saturating_add(1);
                }
            }
        }
        Ok(())
    }
    fn select_output(&mut self) {
        self.output = [[0.0; FRAMES]; 2];
        if self.position < LATENCY_FRAMES {
            return;
        }
        let target = self.position - LATENCY_FRAMES;
        // Pending + four ring slots is the absolute maximum; never drain forever.
        for _ in 0..=CAPACITY {
            let Some(item) = self.pending.take().or_else(|| self.shared.output.pop()) else {
                break;
            };
            if item.epoch != self.epoch || item.start < target {
                self.diagnostics.discarded_responses =
                    self.diagnostics.discarded_responses.saturating_add(1);
            } else if item.start == target {
                self.output = item.samples;
                return;
            } else {
                self.pending = Some(item);
                break;
            }
        }
        self.diagnostics.missing_blocks = self.diagnostics.missing_blocks.saturating_add(1);
    }
}
impl WorkerPort {
    /// Service thread only; skips at most CAPACITY requests from invalid epochs.
    /// Preserve receive/complete order; at most one external request in flight.
    pub fn receive(&mut self) -> Option<WorkItem> {
        for _ in 0..CAPACITY {
            let item = self.shared.input.pop()?;
            if item.epoch == self.shared.epoch.load(Acquire) {
                return Some(item);
            }
        }
        None
    }
    /// Rejects stale epochs, corrupt DSP and full output queues. Never waits.
    pub fn complete(&mut self, item: WorkItem) -> bool {
        if item.epoch != self.shared.epoch.load(Acquire)
            || item.samples.iter().flatten().any(|v| !v.is_finite())
            || !self.shared.output.push(&item)
        {
            self.shared.rejected.fetch_add(1, Relaxed);
            return false;
        }
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn controls_travel_with_packet_and_invalid_control_does_not_advance() {
        let (mut audio, mut worker) = prepare();
        assert_eq!(
            audio.process_with_gain(&mut [[1.0; FRAMES]; 2], 240, f32::NAN),
            Err(AudioError::InvalidControl)
        );
        audio
            .process_with_gain(&mut [[1.0; FRAMES]; 2], 240, 0.5)
            .unwrap();
        audio
            .process_with_gain(&mut [[1.0; FRAMES]; 2], 240, 2.0)
            .unwrap();
        let item = worker.receive().unwrap();
        assert_eq!(item.start_frame(), 0);
        assert_eq!(item.gain(), 0.5);
        audio
            .process_with_gain(&mut [[1.0; FRAMES]; 2], 480, 2.0)
            .unwrap();
        assert_eq!(worker.receive().unwrap().gain(), 2.0);
    }
    #[test]
    fn variable_callbacks_preserve_exact_latency_and_channels() {
        let (mut audio, mut worker) = prepare();
        let mut position = 0;
        for size in [1, 127, 480, 64, 320, 17, 479, 200]
            .into_iter()
            .cycle()
            .take(80)
        {
            let mut samples = [[0.0; FRAMES]; 2];
            let [left, right] = &mut samples;
            for (i, (l, r)) in left.iter_mut().zip(right).take(size).enumerate() {
                *l = (position + i) as f32;
                *r = -(position as f32 + i as f32);
            }
            audio.process(&mut samples, size).unwrap();
            for (i, (l, r)) in samples[0].iter().zip(&samples[1]).take(size).enumerate() {
                let expected = if position + i < LATENCY_FRAMES as usize {
                    0.0
                } else {
                    (position + i - LATENCY_FRAMES as usize) as f32 * 0.5
                };
                assert_eq!(*l, expected);
                assert_eq!(*r, -expected);
            }
            while let Some(mut item) = worker.receive() {
                for v in item.samples.iter_mut().flatten() {
                    *v *= 0.5;
                }
                assert!(worker.complete(item));
            }
            position += size;
        }
        assert_eq!(audio.diagnostics().missing_blocks, 0);
    }
    #[test]
    fn stalled_service_is_bounded_and_old_audio_is_not_replayed() {
        let (mut audio, mut worker) = prepare();
        for _ in 0..20 {
            let mut samples = [[1.0; FRAMES]; 2];
            audio.process(&mut samples, FRAMES).unwrap();
            assert!(samples.iter().flatten().all(|v| *v == 0.0));
        }
        assert_eq!(audio.diagnostics().input_overflows, 16);
        let mut count = 0;
        while let Some(item) = worker.receive() {
            assert!(worker.complete(item));
            count += 1;
        }
        assert_eq!(count, CAPACITY);
        let mut samples = [[1.0; FRAMES]; 2];
        audio.process(&mut samples, FRAMES).unwrap();
        assert!(samples.iter().flatten().all(|v| *v == 0.0));
        assert_eq!(audio.diagnostics().discarded_responses, 4);
    }
    #[test]
    fn reset_invalidates_inflight_pcm_and_instances_are_independent() {
        let (mut audio, mut worker) = prepare();
        let (mut other, mut other_worker) = prepare();
        audio.process(&mut [[1.0; FRAMES]; 2], FRAMES).unwrap();
        other.process(&mut [[2.0; FRAMES]; 2], FRAMES).unwrap();
        let item = worker.receive().unwrap();
        audio.reset().unwrap();
        assert!(!worker.complete(item));
        let mut corrupt = other_worker.receive().unwrap();
        corrupt.samples[0][0] = f32::NAN;
        assert!(!other_worker.complete(corrupt));
        assert_eq!(audio.diagnostics().rejected_completions, 1);
        assert_eq!(other.diagnostics().rejected_completions, 1);
        let mut samples = [[f32::NAN; FRAMES]; 2];
        assert_eq!(
            audio.process(&mut samples, FRAMES),
            Err(AudioError::NonFiniteInput)
        );
        assert_eq!(audio.position, 0);
        assert_eq!(
            audio.process(&mut samples, 0),
            Err(AudioError::InvalidFrames)
        );
    }
    #[test]
    fn concurrent_spsc_preserves_every_packet_without_torn_pcm() {
        let (mut audio, mut worker) = prepare();
        // Test the queue ownership directly, avoiding audio timeline deadlines.
        std::thread::scope(|scope| {
            let thread = scope.spawn(move || {
                for sequence in 0..2000 {
                    let item = loop {
                        if let Some(item) = worker.receive() {
                            break item;
                        }
                        std::thread::yield_now();
                    };
                    assert_eq!(item.start, sequence);
                    assert!(item.samples.iter().flatten().all(|v| *v == sequence as f32));
                }
            });
            for sequence in 0..2000 {
                audio.input.start = sequence;
                audio.input.samples = [[sequence as f32; FRAMES]; 2];
                while !audio.shared.input.push(&audio.input) {
                    std::thread::yield_now();
                }
            }
            thread.join().unwrap();
        });
    }
}
