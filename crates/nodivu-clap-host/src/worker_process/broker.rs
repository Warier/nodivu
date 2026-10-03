//! Blocking service-thread transport. Never call from an audio callback.
use super::mapping::{FRAMES, Mapping};
use serde_json::{Value, json};
use std::{
    io::{self, BufRead, BufReader, Write},
    os::windows::process::CommandExt,
    path::Path,
    process::{Child, Command, Stdio},
    sync::mpsc,
    thread::JoinHandle,
    time::{Duration, SystemTime, UNIX_EPOCH},
};
type Result<T> = std::result::Result<T, Box<dyn std::error::Error>>;
const MAX_CONTROL: usize = 4096;
pub struct Broker {
    child: Child,
    job: Option<super::job::Job>,
    send: Option<mpsc::SyncSender<Vec<u8>>>,
    replies: Option<mpsc::Receiver<io::Result<Vec<u8>>>>,
    threads: Vec<JoinHandle<()>>,
    mapping: Mapping,
    epoch: String,
    sequence: u64,
    failed: bool,
}
impl Broker {
    pub fn start(python: &str, script: &Path) -> Result<Self> {
        Self::start_inner(python, script, false)
    }
    /// Opt-in harness only. Production never exposes fault commands to its worker.
    pub fn start_for_test(python: &str, script: &Path) -> Result<Self> {
        Self::start_inner(python, script, true)
    }
    fn start_inner(python: &str, script: &Path, faults: bool) -> Result<Self> {
        let epoch = format!(
            "{}-{}",
            std::process::id(),
            SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos()
        );
        let name = format!("Local\\NodivuWorker-{epoch}");
        let mapping = Mapping::create(&name)?;
        let mut command = Command::new(python);
        command.args(["-I", "-u"]).arg(script);
        if faults {
            command.arg("--allow-test-faults");
        }
        let mut child = command
            .stdin(Stdio::piped())
            .stdout(Stdio::piped())
            .stderr(Stdio::inherit())
            .creation_flags(0x0800_0000) // CREATE_NO_WINDOW: helper, not an interactive console.
            .spawn()?;
        let job = match super::job::Job::attach(&child) {
            Ok(job) => job,
            Err(error) => {
                let _ = child.kill();
                let _ = child.wait();
                return Err(error.into());
            }
        };
        let (Some(stdin), Some(stdout)) = (child.stdin.take(), child.stdout.take()) else {
            let _ = child.kill();
            let _ = child.wait();
            return Err("worker pipes unavailable".into());
        };
        let (send, receive) = mpsc::sync_channel::<Vec<u8>>(1);
        let (reply, replies) = mpsc::sync_channel(1);
        let error_reply = reply.clone();
        // Own the child before starting helpers: even a thread-start failure reaps it.
        let mut broker = Self {
            child,
            job: Some(job),
            send: Some(send),
            replies: Some(replies),
            threads: Vec::with_capacity(2),
            mapping,
            epoch,
            sequence: 0,
            failed: false,
        };
        let writer = std::thread::Builder::new()
            .name("worker-control-write".into())
            .spawn(move || {
                let mut stdin = stdin;
                while let Ok(bytes) = receive.recv() {
                    if let Err(error) = stdin.write_all(&bytes).and_then(|_| stdin.flush()) {
                        let _ = error_reply.send(Err(error));
                        break;
                    }
                }
            })?;
        broker.threads.push(writer);
        let reader = std::thread::Builder::new()
            .name("worker-control-read".into())
            .spawn(move || {
                let mut reader = BufReader::new(stdout);
                loop {
                    let mut bytes = Vec::with_capacity(MAX_CONTROL);
                    let result = (|| {
                        loop {
                            let buffer = reader.fill_buf()?;
                            if buffer.is_empty() {
                                return Err(io::Error::other("worker EOF"));
                            }
                            let length = buffer
                                .iter()
                                .position(|b| *b == b'\n')
                                .map_or(buffer.len(), |i| i + 1);
                            if bytes.len() + length > MAX_CONTROL {
                                return Err(io::Error::other("oversized worker reply"));
                            }
                            bytes.extend_from_slice(&buffer[..length]);
                            reader.consume(length);
                            if bytes.last() == Some(&b'\n') {
                                return Ok(bytes);
                            }
                        }
                    })();
                    let failed = result.is_err();
                    if reply.send(result).is_err() || failed {
                        break;
                    }
                }
            })?;
        broker.threads.push(reader);
        broker.request(
            json!({"op":"hello","mapping":name,"layout":"f32le-planar-stereo-480"}),
            Duration::from_secs(5),
        )?;
        Ok(broker)
    }
    pub fn request(&mut self, mut command: Value, timeout: Duration) -> Result<Value> {
        if self.failed {
            return Err("worker epoch already invalidated".into());
        }
        self.sequence = self.sequence.checked_add(1).ok_or("sequence overflow")?;
        command["version"] = json!(1);
        command["epoch"] = json!(self.epoch);
        command["seq"] = json!(self.sequence);
        let result = (|| {
            let mut bytes = serde_json::to_vec(&command)?;
            bytes.push(b'\n');
            if bytes.len() > MAX_CONTROL {
                return Err("control frame too large".into());
            }
            self.send.as_ref().ok_or("closed")?.try_send(bytes)?;
            let bytes = self
                .replies
                .as_ref()
                .ok_or("closed")?
                .recv_timeout(timeout)??;
            let reply: Value = serde_json::from_slice(&bytes)?;
            if reply["version"] != 1
                || reply["epoch"] != self.epoch
                || reply["seq"] != self.sequence
                || reply["ok"] != true
            {
                return Err("worker reply does not match current request".into());
            }
            Ok(reply)
        })();
        if result.is_err() {
            self.failed = true;
            self.stop();
        }
        result
    }
    pub fn process(
        &mut self,
        input: &[[f32; FRAMES]; 2],
        frames: usize,
        mode: &str,
        gain: f32,
    ) -> Result<(Value, [[f32; FRAMES]; 2])> {
        if frames == 0
            || frames > FRAMES
            || !gain.is_finite()
            || input
                .iter()
                .any(|c| c[..frames].iter().any(|v| !v.is_finite()))
        {
            return Err("invalid input".into());
        }
        if self.failed {
            return Err("failed epoch".into());
        }
        self.mapping.write_input(input);
        let response = self.request(
            json!({"op":"process","frames":frames,"mode":mode,"gain":gain}),
            Duration::from_secs(2),
        )?;
        if response["frames"] != frames
            || response["produced"]
                .as_u64()
                .is_none_or(|n| n > frames as u64)
            || !response["done"].is_boolean()
        {
            self.failed = true;
            self.stop();
            return Err("invalid audio reply".into());
        }
        let output = self.mapping.read_output();
        if output
            .iter()
            .any(|c| c[..frames].iter().any(|v| !v.is_finite()))
        {
            self.failed = true;
            self.stop();
            return Err("non-finite output".into());
        }
        Ok((response, output))
    }
    fn stop(&mut self) {
        self.send.take();
        self.replies.take(); // Unblock bounded sender threads before joining.
        self.job.take(); // Kill associated descendants before joining pipe readers.
        let _ = self.child.kill();
        let _ = self.child.wait();
        for thread in self.threads.drain(..) {
            let _ = thread.join();
        }
    }
}
impl Drop for Broker {
    fn drop(&mut self) {
        self.stop();
    }
}
