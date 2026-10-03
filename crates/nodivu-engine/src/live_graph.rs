//! WASAPI DAG runtime. All backing memory is allocated before streams start.
use crate::{
    pipeline::Pipeline,
    routing_preview::{FRAMES, Stereo},
};
use nodivu_block::{ParameterEvent, PreparedEffect, StereoMut};
use nodivu_core::{
    live_routing::LivePlan,
    routing::{MAX_DELAY_FRAMES, MAX_NODES, MAX_PORTS},
};

pub(crate) struct Runtime {
    buffers: Vec<Stereo>,
    delays: Vec<[f32; 2]>,
    positions: [[usize; MAX_PORTS]; MAX_NODES],
    pub peaks: [f32; MAX_NODES],
    pub calls: [u64; MAX_NODES],
}
impl Default for Runtime {
    fn default() -> Self {
        Self {
            buffers: vec![[[0.0; FRAMES]; 2]; MAX_NODES],
            delays: vec![[0.0; 2]; MAX_DELAY_FRAMES as usize],
            positions: [[0; MAX_PORTS]; MAX_NODES],
            peaks: [0.0; MAX_NODES],
            calls: [0; MAX_NODES],
        }
    }
}
impl Runtime {
    pub fn reset(&mut self) {
        self.delays.fill([0.0; 2]);
        self.positions.fill([0; MAX_PORTS]);
        self.peaks.fill(0.0);
        self.calls.fill(0);
    }
    pub fn process(
        &mut self,
        plan: &LivePlan,
        pipeline: &mut Pipeline,
        sources: (&Stereo, &Stereo),
        result: &mut Stereo,
        frames: usize,
        effect: &mut crate::external_audio::StereoEffect<'_>,
    ) -> u64 {
        let (capture, tone) = sources;
        let mut errors = 0;
        result.iter_mut().for_each(|c| c[..frames].fill(0.0));
        for (index, step) in plan.steps[..usize::from(plan.count)].iter().enumerate() {
            let mut audio = [[0.0; FRAMES]; 2];
            // Only Mixer sums. Current external profile has one input/output bus;
            // distinct multi-port external profiles must be added explicitly to its adapter.
            for (port, input) in step.inputs.iter().enumerate() {
                if input.source == 0 {
                    continue;
                }
                let source = &self.buffers[usize::from(input.source) - 1];
                for i in 0..frames {
                    let value = if input.delay == 0 {
                        [source[0][i], source[1][i]]
                    } else {
                        let cursor = &mut self.positions[index][port];
                        let at = usize::from(input.offset) + *cursor;
                        let value = self.delays[at];
                        self.delays[at] = [source[0][i], source[1][i]];
                        *cursor = (*cursor + 1) % usize::from(input.delay);
                        value
                    };
                    audio[0][i] += value[0];
                    audio[1][i] += value[1];
                }
            }
            let [left, right] = &mut audio;
            match step.kind {
                1 | 2 if step.enabled => {
                    let source = if step.kind == 1 { capture } else { tone };
                    left[..frames].copy_from_slice(&source[0][..frames]);
                    right[..frames].copy_from_slice(&source[1][..frames]);
                }
                3 => {
                    let slot = usize::from(step.slot);
                    if let Some(spec) = pipeline.plan.gains[slot] {
                        let state = &mut pipeline.slots[slot];
                        let started = std::time::Instant::now();
                        let events = [ParameterEvent {
                            frame_offset: 0,
                            id: nodivu_plugin_gain::AMPLITUDE,
                            value: if spec.bypass {
                                1.0
                            } else {
                                f64::from(spec.amplitude)
                            },
                        }];
                        if state
                            .effect
                            .process(
                                StereoMut::new(&mut left[..frames], &mut right[..frames]),
                                &events,
                            )
                            .is_err()
                        {
                            left[..frames].fill(0.0);
                            right[..frames].fill(0.0);
                            errors += 1;
                        }
                        state.timing.finish(started, frames, 48_000);
                        state.peak = left[..frames]
                            .iter()
                            .chain(&right[..frames])
                            .fold(state.peak, |a, b| a.max(b.abs()));
                    }
                }
                4 | 8
                    if !effect(
                        StereoMut::new(&mut left[..frames], &mut right[..frames]),
                        step.id,
                    ) =>
                {
                    left[..frames].fill(0.0);
                    right[..frames].fill(0.0);
                    errors += 1;
                }
                _ => {}
            }
            if left[..frames]
                .iter()
                .chain(&right[..frames])
                .any(|v| !v.is_finite())
            {
                left[..frames].fill(0.0);
                right[..frames].fill(0.0);
                errors += 1;
            }
            self.peaks[index] = left[..frames]
                .iter()
                .chain(&right[..frames])
                .fold(self.peaks[index], |a, b| a.max(b.abs()));
            self.calls[index] += 1;
            if step.kind == 6 {
                result[0][..frames].copy_from_slice(&left[..frames]);
                result[1][..frames].copy_from_slice(&right[..frames]);
            }
            self.buffers[index][0][..frames].copy_from_slice(&left[..frames]);
            self.buffers[index][1][..frames].copy_from_slice(&right[..frames]);
        }
        errors
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use nodivu_core::{graph::Graph, live_routing::LivePlan};
    use serde_json::json;
    fn id(n: u128) -> String {
        uuid::Uuid::from_u128(n).to_string()
    }
    fn graph() -> Graph {
        serde_json::from_value(json!({"schema_version":2,"nodes":[
            {"id":id(1),"block":{"kind":"tone","enabled":true}},
            {"id":id(2),"block":{"kind":"plugin","source":false,"plugin_id":"delay","parameters":{},"bypass":false}},
            {"id":id(3),"block":{"kind":"mixer"}},
            {"id":id(4),"block":{"kind":"output","endpoint_id":"test"}},
            {"id":id(5),"block":{"kind":"meter"}}
        ],"edges":[
            {"from":id(1),"to":id(2)},
            {"from":id(2),"to":id(3),"to_port":0},
            {"from":id(1),"to":id(3),"to_port":1},
            {"from":id(3),"to":id(4)},
            {"from":id(1),"to":id(5)}
        ]})).unwrap()
    }
    #[test]
    fn production_executor_aligns_impulse_and_isolates_fanout_across_quantums() {
        let graph = graph();
        graph.validate().unwrap();
        let render = graph.live_plan(false, |_| 4).unwrap();
        let plan = render.live.unwrap();
        assert_eq!(LivePlan::from_words(&plan.words()), plan);
        let mut pipeline = Pipeline::default();
        pipeline.apply(render);
        let mut runtime = Runtime::default();
        let silence = [[0.0; FRAMES]; 2];
        let mut result = silence;
        let mut delay = [[0.0; 2]; 4];
        let mut cursor = 0;
        let mut clock = 0;
        let mut calls = 0;
        for frames in [3, 1, 7, 480] {
            let mut source = silence;
            if clock == 0 {
                source[0][0] = 1.0;
                source[1][0] = -0.5;
            }
            let mut effect = |audio: StereoMut<'_>, _| {
                calls += 1;
                for (l, r) in audio.left.iter_mut().zip(audio.right) {
                    let input = [*l, *r];
                    [*l, *r] = delay[cursor];
                    delay[cursor] = input;
                    cursor = (cursor + 1) % 4;
                }
                true
            };
            assert_eq!(
                runtime.process(
                    &plan,
                    &mut pipeline,
                    (&silence, &source),
                    &mut result,
                    frames,
                    &mut effect
                ),
                0
            );
            for (i, (left, right)) in result[0][..frames]
                .iter()
                .zip(&result[1][..frames])
                .enumerate()
            {
                assert_eq!(*left, if clock + i == 4 { 2.0 } else { 0.0 });
                assert_eq!(*right, if clock + i == 4 { -1.0 } else { 0.0 });
            }
            clock += frames;
        }
        assert_eq!(calls, 4);
        assert_eq!(runtime.calls[..usize::from(plan.count)], [4; 5]);
        let meter = plan.steps.iter().position(|s| s.kind == 7).unwrap();
        assert_eq!(runtime.peaks[meter], 1.0);
        runtime.reset();
        assert!(runtime.delays.iter().flatten().all(|x| *x == 0.0));
    }
    #[test]
    fn failure_silences_only_failed_branch_before_mixing_and_malformed_plan_is_rejected() {
        let graph = graph();
        let render = graph.live_plan(false, |_| 0).unwrap();
        let plan = render.live.unwrap();
        let mut pipeline = Pipeline::default();
        pipeline.apply(render);
        let mut runtime = Runtime::default();
        let input = [[0.25; FRAMES]; 2];
        let mut out = [[0.0; FRAMES]; 2];
        assert_eq!(
            runtime.process(
                &plan,
                &mut pipeline,
                (&input, &input),
                &mut out,
                FRAMES,
                &mut |audio, _| {
                    audio.left.fill(f32::NAN);
                    true
                }
            ),
            1
        );
        assert_eq!(out, input);
        let mut bad = render;
        bad.live.as_mut().unwrap().steps[0].inputs[0].source = 1;
        assert!(bad.validate().is_err());
        bad = render;
        bad.external.fill(None);
        assert!(bad.validate().is_err());
    }
}
