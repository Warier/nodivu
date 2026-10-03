//! Bus estéreo 48 kHz prealocado. Um único worker possui ambos os cursores.
use crate::diagnostics::{STAGES, Timing};
use nodivu_block::{ParameterEvent, PreparedEffect, PreparedSource, StereoMut};
use nodivu_plugin_gain::{AMPLITUDE, Gain};
use nodivu_plugin_tone::Tone;
use std::time::Instant;
pub const RATE: u32 = 48_000;
pub const BLOCK_FRAMES: usize = 480;
#[cfg(test)]
const CAPACITY_FRAMES: usize = 4_800;
#[cfg(test)]
pub const TARGET_FRAMES: usize = 960;

#[derive(Default)]
pub struct Counters {
    pub underruns: u64,
    pub overflows: u64,
    pub invalid_samples: u64,
    pub clipped_samples: u64,
    pub processor_errors: u64,
}

/// PI lento sobre ocupação filtrada; correção máxima de ±1000 ppm.
pub struct ClockCorrection {
    target: f64,
    filtered: f64,
    integral: f64,
}
impl ClockCorrection {
    pub fn new(target: usize) -> Self {
        Self {
            target: target as f64,
            filtered: 0.0,
            integral: 0.0,
        }
    }
    pub fn ratio(&mut self, occupancy: f64, frames: usize) -> f64 {
        let dt = frames as f64 / f64::from(RATE);
        let error = (occupancy - self.target) / self.target;
        self.filtered += (dt / (1.0 + dt)) * (error - self.filtered);
        self.integral = (self.integral + self.filtered * dt * 0.0003).clamp(-0.001, 0.001);
        1.0 + (self.filtered * 0.002 + self.integral).clamp(-0.001, 0.001)
    }
}

pub struct Bus {
    ring: Box<[[f32; 2]]>,
    nominal_ratio: f64,
    target_frames: usize,
    read: usize,
    len: usize,
    fraction: f64,
    primed: bool,
    correction: ClockCorrection,
    gain: Gain,
    amplitude: f32,
    planar: [[f32; BLOCK_FRAMES]; 2],
    pub routing: crate::live_graph::Runtime,
    pub pipeline: crate::pipeline::Pipeline,
    pending_plan: Option<nodivu_core::graph::RenderPlan>,
    transition_frames: usize,
    stopping: bool,
    pub timings: [Timing; STAGES],
    tone: Tone,
    source: u32,
    pub counters: Counters,
    pub input_peak: f32,
    pub output_peak: f32,
    pub external_timing: Timing,
    pub external_input_peak: f32,
    pub external_output_peak: f32,
}
impl Bus {
    pub fn new(capture_rate: u32) -> Self {
        Self::with_target_ms(capture_rate, 20)
    }
    pub(crate) fn with_target_ms(capture_rate: u32, target_ms: u32) -> Self {
        Self {
            ring: vec![[0.0; 2]; capture_rate as usize / 10].into_boxed_slice(),
            nominal_ratio: f64::from(capture_rate) / f64::from(RATE),
            target_frames: (capture_rate * target_ms / 1000) as usize,
            read: 0,
            len: 0,
            fraction: 0.0,
            primed: false,
            correction: ClockCorrection::new((RATE * target_ms / 1000) as usize),
            gain: Gain::default(),
            amplitude: 0.0,
            planar: [[0.0; BLOCK_FRAMES]; 2],
            pipeline: crate::pipeline::Pipeline::default(),
            routing: crate::live_graph::Runtime::default(),
            pending_plan: None,
            transition_frames: 0,
            stopping: false,
            timings: [Timing::default(); STAGES],
            tone: Tone::default(),
            source: 1,
            counters: Counters::default(),
            input_peak: 0.0,
            output_peak: 0.0,
            external_timing: Timing::default(),
            external_input_peak: 0.0,
            external_output_peak: 0.0,
        }
    }
    pub fn occupancy(&self) -> usize {
        self.len
    }
    pub fn target_frames(&self) -> usize {
        self.target_frames
    }
    pub fn clear_capture(&mut self) {
        self.len = 0;
        self.fraction = 0.0;
        self.primed = false;
        self.gain.reset();
        self.correction =
            ClockCorrection::new((self.target_frames as f64 / self.nominal_ratio).round() as usize);
    }
    pub fn push(&mut self, mut frame: [f32; 2]) {
        for sample in &mut frame {
            if !sample.is_finite() {
                *sample = 0.0;
                self.counters.invalid_samples += 1;
            }
            self.input_peak = self.input_peak.max(sample.abs());
        }
        if self.len == self.ring.len() {
            self.counters.overflows += 1;
            return;
        }
        let write = (self.read + self.len) % self.ring.len();
        self.ring[write] = frame;
        self.len += 1;
    }
    pub fn set_signal(&mut self, plan: nodivu_core::graph::SignalPlan) {
        // Ao mudar de fonte, começar com ganho zero evita um salto na retomada.
        // Desconexão mantém a fonte anterior durante a rampa para silêncio.
        if plan.source != 0 && self.source != plan.source {
            self.source = plan.source;
            self.gain = Gain::default();
        }
        self.set_target(plan.amplitude);
    }
    pub fn set_target(&mut self, amplitude: f32) {
        self.amplitude = amplitude;
    }
    pub fn request_plan(&mut self, plan: nodivu_core::graph::RenderPlan) {
        if let Some(pending) = &mut self.pending_plan {
            *pending = plan; // Coalescer sem recomeçar a contagem de fade.
        } else if self.pipeline.plan != plan {
            if self.pipeline.plan.same_route(&plan) {
                self.pipeline.apply(plan);
                self.set_signal(plan.signal);
            } else {
                // Troca estrutural: 10 ms para zero, commit, 10 ms de retomada.
                // Mantém streams; não promete troca estrutural sem breve atenuação.
                self.pending_plan = Some(plan);
                self.transition_frames = BLOCK_FRAMES;
                self.set_target(0.0);
            }
        }
    }
    pub fn begin_stop(&mut self) {
        self.stopping = true;
    }
    #[cfg(test)]
    pub fn render(&mut self, output: &mut [[f32; 2]]) {
        self.render_with_effect(output, &mut None);
    }
    pub fn render_with_effect(
        &mut self,
        output: &mut [[f32; 2]],
        effect: &mut Option<&mut crate::external_audio::StereoEffect<'_>>,
    ) {
        // O contrato rejeita a aquisição antes de acessar os buffers prealocados.
        if output.is_empty() {
            return;
        }
        if output.len() > BLOCK_FRAMES {
            output.fill([0.0; 2]);
            self.counters.processor_errors += 1;
            return;
        }
        let start = Instant::now();
        let live = self.pipeline.plan.live;
        let mut capture = [[0.0; BLOCK_FRAMES]; 2];
        let mut generated = [[0.0; BLOCK_FRAMES]; 2];
        if let Some(plan) = live {
            if plan.steps[..usize::from(plan.count)]
                .iter()
                .any(|s| s.kind == 1 && s.enabled)
            {
                self.render_capture(output);
                for (i, frame) in output.iter().enumerate() {
                    capture[0][i] = frame[0];
                    capture[1][i] = frame[1];
                }
            } else {
                self.len = 0;
                self.primed = false;
            }
            if plan.steps[..usize::from(plan.count)]
                .iter()
                .any(|s| s.kind == 2 && s.enabled)
            {
                let [l, r] = &mut generated;
                if self
                    .tone
                    .generate(
                        StereoMut::new(&mut l[..output.len()], &mut r[..output.len()]),
                        &[],
                    )
                    .is_err()
                {
                    self.counters.processor_errors += 1;
                }
                self.input_peak = l[..output.len()]
                    .iter()
                    .fold(self.input_peak, |a, b| a.max(b.abs()));
            }
            output.fill([0.0; 2]);
        } else if self.source == 2 {
            let [left, right] = &mut self.planar;
            if self
                .tone
                .generate(
                    StereoMut::new(&mut left[..output.len()], &mut right[..output.len()]),
                    &[],
                )
                .is_err()
            {
                left[..output.len()].fill(0.0);
                right[..output.len()].fill(0.0);
                self.counters.processor_errors += 1;
            }
            self.len = 0;
            self.primed = false;
            for sample in &left[..output.len()] {
                self.input_peak = self.input_peak.max(sample.abs());
            }
        } else if self.source == 3 {
            output.fill([0.0; 2]);
        } else {
            self.render_capture(output);
        }
        self.timings[1].finish(start, output.len(), RATE);
        let start = Instant::now();
        let [left, right] = &mut self.planar;
        if self.source != 2 {
            for (i, frame) in output.iter().enumerate() {
                left[i] = frame[0];
                right[i] = frame[1];
            }
            self.timings[2].finish(start, output.len(), RATE);
        }
        let start = Instant::now();
        let mut measured = |audio: StereoMut<'_>, id| {
            let Some(effect) = effect.as_deref_mut() else {
                return false;
            };
            for sample in audio.left.iter().chain(audio.right.iter()) {
                self.external_input_peak = self.external_input_peak.max(sample.abs());
            }
            let started = Instant::now();
            let frames = audio.left.len();
            let ok = effect(StereoMut::new(audio.left, audio.right), id);
            self.external_timing.finish(started, frames, RATE);
            for sample in audio.left.iter().chain(audio.right.iter()) {
                self.external_output_peak = self.external_output_peak.max(sample.abs());
            }
            ok
        };
        if let Some(plan) = live {
            let mut result = [[0.0; BLOCK_FRAMES]; 2];
            self.counters.processor_errors += self.routing.process(
                &plan,
                &mut self.pipeline,
                (&capture, &generated),
                &mut result,
                output.len(),
                &mut measured,
            );
            left[..output.len()].copy_from_slice(&result[0][..output.len()]);
            right[..output.len()].copy_from_slice(&result[1][..output.len()]);
        } else {
            self.counters.processor_errors += self.pipeline.process_with_effect(
                &mut left[..output.len()],
                &mut right[..output.len()],
                &mut measured,
            );
        }
        // Detached harnesses explicitly install an external step too; no implicit final effect.
        let events = [ParameterEvent {
            frame_offset: 0,
            id: AMPLITUDE,
            value: if self.stopping {
                0.0
            } else {
                f64::from(self.amplitude)
            },
        }];
        if self
            .gain
            .process(
                StereoMut::new(&mut left[..output.len()], &mut right[..output.len()]),
                &events,
            )
            .is_err()
        {
            left[..output.len()].fill(0.0);
            right[..output.len()].fill(0.0);
            self.counters.processor_errors += 1;
        }
        self.timings[3].finish(start, output.len(), RATE);
        let start = Instant::now();
        for (i, frame) in output.iter_mut().enumerate() {
            *frame = [left[i], right[i]];
            for sample in frame {
                if !sample.is_finite() {
                    *sample = 0.0;
                    self.counters.invalid_samples += 1;
                }
                self.output_peak = self.output_peak.max(sample.abs());
                if sample.abs() > 1.0 {
                    self.counters.clipped_samples += 1;
                }
                *sample = sample.clamp(-1.0, 1.0);
            }
        }
        self.timings[4].finish(start, output.len(), RATE);
        if self.pending_plan.is_some() {
            self.transition_frames = self.transition_frames.saturating_sub(output.len());
            if self.transition_frames == 0
                && let Some(plan) = self.pending_plan.take()
            {
                self.routing.reset();
                self.pipeline.apply(plan);
                self.set_signal(plan.signal);
            }
        }
    }
    fn render_capture(&mut self, output: &mut [[f32; 2]]) {
        // Chamador segmenta eventos maiores; DSP nunca processa mais de 480 frames.
        let ratio = self.nominal_ratio
            * self
                .correction
                .ratio(self.len as f64 / self.nominal_ratio, output.len());
        for frame in output {
            if !self.primed && self.len >= self.target_frames {
                self.primed = true;
            }
            if !self.primed {
                *frame = [0.0; 2];
                continue;
            }
            if self.len < 2 {
                self.counters.underruns += 1;
                self.clear_capture();
                *frame = [0.0; 2];
                continue;
            }
            let a = self.ring[self.read];
            let b = self.ring[(self.read + 1) % self.ring.len()];
            for ch in 0..2 {
                frame[ch] = a[ch] + (b[ch] - a[ch]) * self.fraction as f32;
            }
            self.fraction += ratio;
            let consumed = (self.fraction as usize).min(self.len);
            self.fraction -= consumed as f64;
            self.read = (self.read + consumed) % self.ring.len();
            self.len -= consumed;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn app_capture_and_plugin_reach_output_at_unity_without_hidden_attenuation() {
        use nodivu_core::graph::Graph;
        use serde_json::json;
        for block in [
            json!({"kind":"capture","endpoint_id":"mic"}),
            json!({"kind":"plugin","plugin_id":"test.source","source":true,"consumer":false,"parameters":{},"bypass":false}),
        ] {
            let graph: Graph = serde_json::from_value(json!({"schema_version":2,"nodes":[
                {"id":uuid::Uuid::from_u128(1),"block":block},
                {"id":uuid::Uuid::from_u128(2),"block":{"kind":"output","endpoint_id":"phones"}}
            ],"edges":[{"from":uuid::Uuid::from_u128(1),"to":uuid::Uuid::from_u128(2),"from_port":0,"to_port":0}]})).unwrap();
            let mut bus = Bus::new(RATE);
            bus.request_plan(graph.live_plan(false, |_| 0).unwrap());
            for _ in 0..TARGET_FRAMES {
                bus.push([0.25; 2]);
            }
            let mut out = [[0.0; 2]; BLOCK_FRAMES];
            let mut source = |audio: StereoMut<'_>, _| {
                audio.left.fill(0.25);
                audio.right.fill(0.25);
                true
            };
            for _ in 0..12 {
                for _ in 0..BLOCK_FRAMES {
                    bus.push([0.25; 2]);
                }
                bus.render_with_effect(&mut out, &mut Some(&mut source));
            }
            assert!(
                out.iter().flatten().all(|v| (*v - 0.25).abs() < 0.00001),
                "unity path attenuated: {:?}",
                out[479]
            );
            bus.request_plan(graph.live_plan(true, |_| 0).unwrap());
            for _ in 0..4 {
                bus.render_with_effect(&mut out, &mut Some(&mut source));
            }
            assert!(out.iter().flatten().all(|v| *v == 0.0));
        }
    }
    #[test]
    fn external_effect_receives_audio_before_master_and_errors_silence_the_block() {
        let mut bus = Bus::new(RATE);
        for _ in 0..2000 {
            bus.push([0.5; 2]);
        }
        bus.set_target(0.0);
        bus.pipeline.plan.external[0] = Some(nodivu_core::graph::NodeId(uuid::Uuid::nil()));
        bus.pipeline.plan.order[0] = 8;
        bus.pipeline.plan.count = 1;
        let mut out = [[0.0; 2]; BLOCK_FRAMES];
        let mut calls = 0;
        let mut effect = |audio: StereoMut<'_>, _| {
            calls += 1;
            assert_eq!(audio.left.len(), BLOCK_FRAMES);
            assert!(audio.left.iter().all(|v| *v == 0.5));
            for v in audio.left.iter_mut().chain(audio.right) {
                *v *= 0.5;
            }
            true
        };
        bus.render_with_effect(&mut out, &mut Some(&mut effect));
        assert_eq!(calls, 1);
        assert_eq!(bus.external_input_peak, 0.5);
        assert_eq!(bus.external_output_peak, 0.25);
        assert!(out.iter().flatten().all(|v| *v == 0.0));
        assert_eq!(bus.external_timing.calls, 1);
        bus.set_target(1.0);
        let mut failure = |audio: StereoMut<'_>, _| {
            audio.left.fill(1.0);
            audio.right.fill(1.0);
            false
        };
        bus.render_with_effect(&mut out, &mut Some(&mut failure));
        assert_eq!(bus.counters.processor_errors, 1);
        assert!(out.iter().flatten().all(|v| *v == 0.0));
        assert_eq!(bus.external_timing.calls, 2);
    }
    #[test]
    fn external_effect_is_not_called_for_empty_or_oversized_blocks() {
        let mut bus = Bus::new(RATE);
        let mut called = false;
        let mut effect = |_: StereoMut<'_>, _| {
            called = true;
            true
        };
        let mut oversized = [[1.0; 2]; BLOCK_FRAMES + 1];
        bus.render_with_effect(&mut [], &mut Some(&mut effect));
        bus.render_with_effect(&mut oversized, &mut Some(&mut effect));
        assert!(!called);
        assert_eq!(bus.counters.processor_errors, 1);
        assert!(oversized.iter().flatten().all(|v| *v == 0.0));
    }
    #[test]
    fn converts_common_capture_rates_without_duration_drift_or_hidden_attenuation() {
        for rate in [
            8_000, 11_025, 16_000, 22_050, 24_000, 32_000, 44_100, 48_000,
        ] {
            let mut bus = Bus::new(rate);
            let mut input_frame = 0;
            let mut push = |bus: &mut Bus, count| {
                for _ in 0..count {
                    let sample = (2.0 * std::f32::consts::PI * 400.0 * input_frame as f32
                        / rate as f32)
                        .sin()
                        * 0.5;
                    bus.push([sample; 2]);
                    input_frame += 1;
                }
            };
            let target = rate / 50;
            push(&mut bus, target);
            bus.set_target(1.0);
            let mut out = [[0.0; 2]; BLOCK_FRAMES];
            let mut squares = 0.0_f64;
            let mut count = 0;
            for cycle in 0..1000 {
                bus.render(&mut out);
                if cycle > 10 {
                    squares += out.iter().map(|f| f64::from(f[0]).powi(2)).sum::<f64>();
                    count += BLOCK_FRAMES;
                }
                // Taxas como 11025 não têm número inteiro de frames em 10 ms.
                push(&mut bus, (cycle + 1) * rate / 100 - cycle * rate / 100);
            }
            assert_eq!(bus.counters.underruns, 0, "rate {rate}");
            assert_eq!(bus.counters.overflows, 0, "rate {rate}");
            assert!(
                (bus.occupancy() as i32 - target as i32).abs() < 5,
                "rate {rate}"
            );
            assert!(
                ((squares / count as f64).sqrt() - 0.5 / 2.0_f64.sqrt()).abs() < 0.005,
                "rate {rate}"
            );
        }
    }
    #[test]
    fn gain_mute_ramp_and_numeric_protection() {
        let mut bus = Bus::new(RATE);
        for _ in 0..2000 {
            bus.push([1.0, 1.0]);
        }
        let gain = 10.0_f32.powf(-6.0 / 20.0);
        bus.set_target(gain);
        let mut out = [[0.0; 2]; BLOCK_FRAMES];
        bus.render(&mut out);
        assert!((out[479][0] - 0.501187).abs() < 0.00001);
        assert!(out.windows(2).all(|p| p[1][0] >= p[0][0]));
        bus.set_target(0.0);
        bus.render(&mut out);
        assert_eq!(out[479], [0.0; 2]);
        bus.render(&mut out);
        assert!(out.iter().all(|f| *f == [0.0; 2]));
        let mut bus = Bus::new(RATE);
        bus.push([f32::NAN, f32::INFINITY]);
        for _ in 0..2000 {
            bus.push([2.0, -2.0]);
        }
        bus.set_target(1.0);
        bus.render(&mut out);
        assert_eq!(bus.counters.invalid_samples, 2);
        assert!(bus.counters.clipped_samples > 0);
        assert!(
            out.iter()
                .flatten()
                .all(|s| s.is_finite() && s.abs() <= 1.0)
        );
    }
    #[test]
    fn bounded_overflow_and_reprime_after_starvation() {
        let mut bus = Bus::new(RATE);
        for _ in 0..CAPACITY_FRAMES + 50 {
            bus.push([0.5; 2]);
        }
        assert_eq!(bus.occupancy(), CAPACITY_FRAMES);
        assert_eq!(bus.counters.overflows, 50);
        let mut out = [[0.0; 2]; BLOCK_FRAMES];
        for _ in 0..20 {
            bus.render(&mut out);
        }
        assert_eq!(bus.counters.underruns, 1);
        assert!(out.iter().all(|f| *f == [0.0; 2]));
        for _ in 0..TARGET_FRAMES {
            bus.push([0.5; 2]);
        }
        bus.set_target(1.0);
        bus.render(&mut out);
        assert!(out[479][0] > 0.0);
    }
    #[test]
    fn independent_clocks_converge_for_thirty_minutes_without_drops() {
        for target in [480, 720, 960] {
            for ppm in [-500.0, -100.0, 100.0, 500.0] {
                let mut correction = ClockCorrection::new(target);
                let mut occupancy = target as f64;
                for _ in 0..180_000 {
                    let ratio = correction.ratio(occupancy, BLOCK_FRAMES);
                    occupancy += BLOCK_FRAMES as f64 * (1.0 + ppm / 1_000_000.0 - ratio);
                    assert!((target as f64 * 0.5..target as f64 * 1.5).contains(&occupancy));
                }
                assert!((occupancy - target as f64).abs() < 1.0);
            }
        }
    }
    #[test]
    fn smaller_target_changes_priming_without_changing_capacity() {
        for rate in [44_100, 48_000] {
            let mut low = Bus::with_target_ms(rate, 10);
            let mut stable = Bus::new(rate);
            for _ in 0..rate / 100 {
                low.push([1.0; 2]);
                stable.push([1.0; 2]);
            }
            low.set_target(1.0);
            stable.set_target(1.0);
            let mut out = [[0.0; 2]; 64];
            stable.render(&mut out);
            assert!(out.iter().all(|f| *f == [0.0; 2]));
            low.render(&mut out);
            assert!(out[63][0] > 0.0);
            assert_eq!(low.ring.len(), stable.ring.len());
        }
    }
    #[test]
    fn topology_transition_is_bounded_and_parameter_update_does_not_restart_it() {
        use nodivu_core::graph::{GainStep, NodeId, RenderPlan, SignalPlan};
        let mut bus = Bus::new(RATE);
        let mut plan = RenderPlan {
            signal: SignalPlan {
                source: 2,
                amplitude: 1.0,
            },
            count: 1,
            ..RenderPlan::default()
        };
        plan.gains[0] = Some(GainStep {
            id: NodeId(uuid::Uuid::from_u128(1)),
            amplitude: 0.5,
            bypass: false,
        });
        let mut output = [[0.0; 2]; 64];
        for _ in 0..8 {
            bus.request_plan(plan);
            bus.render(&mut output);
        }
        assert!(bus.pending_plan.is_none());
        assert_eq!(bus.pipeline.plan, plan);
        for _ in 0..8 {
            bus.render(&mut output);
        }
        let calls = bus.pipeline.slots[0].timing.calls;
        plan.gains[0].as_mut().unwrap().amplitude = 0.25;
        bus.request_plan(plan);
        assert!(bus.pending_plan.is_none());
        bus.render(&mut output);
        assert_eq!(bus.pipeline.slots[0].timing.calls, calls + 1);
        plan.count = 0;
        plan.signal = SignalPlan::default();
        for _ in 0..16 {
            bus.request_plan(plan);
            bus.render(&mut output);
        }
        assert!(output.iter().flatten().all(|v| *v == 0.0));
        assert!(bus.pending_plan.is_none());
    }
}
