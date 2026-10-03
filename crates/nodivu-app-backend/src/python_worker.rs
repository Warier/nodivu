//! Local worker packages and per-instance services; no Python/IPC on the audio thread.
use nodivu_block::StereoMut;
use nodivu_clap_host::{
    worker_audio::{self, AudioBridge, FRAMES, LATENCY_FRAMES},
    worker_process::Broker,
};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{
    io::Read,
    path::{Path, PathBuf},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU8, AtomicU64, Ordering::*},
    },
    thread::JoinHandle,
    time::Duration,
};

#[derive(Clone, Deserialize)]
#[serde(deny_unknown_fields)]
pub struct Package {
    pub schema_version: u32,
    pub plugin_id: String,
    pub title: String,
    pub description: String,
    pub accent: String,
    pub python: String,
    pub script: PathBuf,
    pub profile: String,
}
impl Package {
    pub fn read(path: &Path) -> Result<Self, Box<dyn std::error::Error>> {
        let mut bytes = Vec::new();
        std::fs::File::open(path)?
            .take(65537)
            .read_to_end(&mut bytes)?;
        if bytes.len() > 65536 {
            return Err("Worker manifesto excede 64 KiB".into());
        }
        let mut p: Self = serde_json::from_value(nodivu_core::json::parse(&bytes)?)?;
        let bounded = |s: &str, n: usize| !s.is_empty() && s.len() <= n;
        if p.schema_version != 1
            || p.profile != "effect-gain-v1"
            || !bounded(&p.plugin_id, 128)
            || !bounded(&p.title, 64)
            || !bounded(&p.description, 240)
            || !bounded(&p.python, 4096)
            || p.accent.len() != 7
            || !p.accent.starts_with('#')
            || !p.accent[1..].bytes().all(|c| c.is_ascii_hexdigit())
        {
            return Err("Manifesto de worker inválido".into());
        }
        let parent = path
            .canonicalize()?
            .parent()
            .ok_or("Pasta ausente")?
            .to_owned();
        if p.script.is_absolute() {
            return Err("script deve ser relativo ao pacote".into());
        }
        p.script = parent.join(&p.script).canonicalize()?;
        if !p.script.starts_with(&parent) || !p.script.is_file() {
            return Err("script deve ficar dentro do pacote".into());
        }
        Ok(p)
    }
    pub fn catalog(&self) -> Value {
        json!({"id":self.plugin_id,"name":self.title,"source":false,"consumer":false,"inputs":[0],"outputs":[0],"file_player":false,"worker":true,"latency_frames":LATENCY_FRAMES,
            "parameters":[{"id":7,"min":0.0,"max":4.0,"default":1.0,"readonly":false,"stepped":false}],
            "appearance":{"title":self.title,"description":self.description,"accent":self.accent,"layout":"stack",
                "controls":[{"parameter":7,"label":"Intensidade","widget":"slider","unit":"×","group":"Processamento externo"}]}})
    }
}

#[derive(Default)]
pub struct State {
    pub active: AtomicBool,
    pub restart: AtomicU64,
    stop: AtomicBool,
    status: AtomicU8,             // 0 waiting, 1 loading, 2 running, 3 failed
    error: Mutex<Option<String>>, // Main/service only. Never locked by audio.
    missing: AtomicU64,
    overflow: AtomicU64,
    discarded: AtomicU64,
    completed: AtomicU64,
}
impl State {
    pub fn snapshot(&self) -> Value {
        json!({"worker":true,"status":self.status.load(Relaxed),"latency_ms":20,"latency_frames":LATENCY_FRAMES,
            "error":self.error.lock().ok().and_then(|e|e.clone()),"missing_blocks":self.missing.load(Relaxed),
            "input_overflows":self.overflow.load(Relaxed),"discarded_responses":self.discarded.load(Relaxed),"completed_blocks":self.completed.load(Relaxed)})
    }
    fn fail(&self, error: String) {
        if let Ok(mut message) = self.error.lock() {
            *message = Some(error.chars().take(512).collect());
        }
        self.status.store(3, Release);
    }
}
pub struct Instance {
    audio: AudioBridge,
    state: Arc<State>,
    service: Option<JoinHandle<()>>,
    generation: u64,
    dry: [[f32; LATENCY_FRAMES as usize]; 2],
    position: usize,
    wet: f32,
}
impl Instance {
    /// Prepares only buffers/thread. Python starts on the first routed audio packet.
    pub fn prepare(package: Package, state: Arc<State>) -> std::io::Result<Self> {
        let (audio, mut port) = worker_audio::prepare();
        let shared = Arc::clone(&state);
        let service = std::thread::Builder::new()
            .name("nodivu-python-worker".into())
            .spawn(move || {
                let mut broker: Option<Broker> = None;
                let mut epoch = None;
                let mut failed = None;
                while !shared.stop.load(Acquire) {
                    if !shared.active.load(Acquire) {
                        broker = None;
                        epoch = None;
                        failed = None;
                        shared.status.store(0, Release);
                        std::thread::sleep(Duration::from_millis(5));
                        continue;
                    }
                    let Some(mut item) = port.receive() else {
                        std::thread::sleep(Duration::from_millis(1));
                        continue;
                    };
                    if failed == Some(item.epoch()) {
                        continue;
                    }
                    let result = (|| -> Result<(), Box<dyn std::error::Error>> {
                        if epoch != Some(item.epoch()) {
                            // An explicit restart or a new UUID never reuses a potentially corrupt process.
                            broker = None;
                            shared.status.store(1, Release);
                            if let Ok(mut error) = shared.error.lock() {
                                *error = None;
                            }
                            broker = Some(Broker::start(&package.python, &package.script)?);
                            epoch = Some(item.epoch());
                            failed = None;
                        }
                        let peer = broker.as_mut().ok_or("Worker ausente")?;
                        let (_, output) =
                            peer.process(&item.samples, FRAMES, "effect", item.gain())?;
                        item.samples = output;
                        Ok(())
                    })();
                    match result {
                        Ok(()) => {
                            if port.complete(item) {
                                shared.completed.fetch_add(1, Relaxed);
                            }
                            shared.status.store(2, Release);
                        }
                        Err(error) => {
                            failed = Some(item.epoch());
                            broker = None;
                            shared.fail(error.to_string());
                        }
                    }
                }
                // Dropping Broker here kills/reaps child and joins pipe helpers outside audio.
            })?;
        Ok(Self {
            audio,
            state,
            service: Some(service),
            generation: 0,
            dry: [[0.0; LATENCY_FRAMES as usize]; 2],
            position: 0,
            wet: 1.0,
        })
    }
    pub fn reset(&mut self) -> Result<(), nodivu_clap_host::worker_audio::AudioError> {
        self.audio.reset()?;
        self.dry = [[0.0; LATENCY_FRAMES as usize]; 2];
        self.position = 0;
        self.generation = self.state.restart.load(Acquire);
        Ok(())
    }
    pub fn process(
        &mut self,
        audio: StereoMut<'_>,
        gain: f32,
        bypass: bool,
    ) -> Result<(), nodivu_clap_host::worker_audio::AudioError> {
        if self.generation != self.state.restart.load(Acquire) {
            self.reset()?;
        }
        let frames = audio.left.len();
        if frames == 0 || frames > FRAMES || frames != audio.right.len() {
            return Err(worker_audio::AudioError::InvalidFrames);
        }
        let mut samples = [[0.0; FRAMES]; 2];
        samples[0][..frames].copy_from_slice(audio.left);
        samples[1][..frames].copy_from_slice(audio.right);
        self.audio.process_with_gain(&mut samples, frames, gain)?;
        for ((left, right), (wet_left, wet_right)) in audio
            .left
            .iter_mut()
            .zip(audio.right.iter_mut())
            .zip(samples[0].iter().zip(&samples[1]))
            .take(frames)
        {
            let dry = [self.dry[0][self.position], self.dry[1][self.position]];
            self.dry[0][self.position] = *left;
            self.dry[1][self.position] = *right;
            self.position = (self.position + 1) % LATENCY_FRAMES as usize;
            self.wet = if bypass {
                (self.wet - 1.0 / 480.0).max(0.0)
            } else {
                (self.wet + 1.0 / 480.0).min(1.0)
            };
            *left = wet_left * self.wet + dry[0] * (1.0 - self.wet);
            *right = wet_right * self.wet + dry[1] * (1.0 - self.wet);
        }
        let d = self.audio.diagnostics();
        self.state.missing.store(d.missing_blocks, Relaxed);
        self.state.overflow.store(d.input_overflows, Relaxed);
        self.state.discarded.store(d.discarded_responses, Relaxed);
        Ok(())
    }
}
impl Drop for Instance {
    fn drop(&mut self) {
        self.state.stop.store(true, Release);
        if let Some(thread) = self.service.take() {
            let _ = thread.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn bypass_is_delayed_and_reset_does_not_replay_old_input() {
        let (audio, _port) = worker_audio::prepare();
        // No process in this unit test: missing wet signal, explicit delayed dry bypass.
        let mut instance = Instance {
            audio,
            state: Arc::new(State::default()),
            service: None,
            generation: 0,
            dry: [[0.0; LATENCY_FRAMES as usize]; 2],
            position: 0,
            wet: 1.0,
        };
        for block in 0..5 {
            let mut l = [(block + 1) as f32; FRAMES];
            let mut r = [-(block as f32 + 1.0); FRAMES];
            instance
                .process(StereoMut::new(&mut l, &mut r), 1.0, true)
                .unwrap();
            let expected = if block < 2 { 0.0 } else { (block - 1) as f32 };
            assert!(l.iter().all(|v| (*v - expected).abs() < 0.0001));
            assert!(r.iter().all(|v| (*v + expected).abs() < 0.0001));
        }
        instance.reset().unwrap();
        let mut l = [0.0; FRAMES];
        let mut r = [0.0; FRAMES];
        instance
            .process(StereoMut::new(&mut l, &mut r), 1.0, true)
            .unwrap();
        assert!(l.iter().chain(&r).all(|v| *v == 0.0));
    }
}
