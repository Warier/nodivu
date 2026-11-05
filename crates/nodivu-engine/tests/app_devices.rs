//! Offline selection regression. The fake explicitly refuses to open audio;
//! successful graph validation is never evidence of working virtual hardware.
use nodivu_core::*;
use nodivu_engine::{AudioConfig, AudioSession, BackendError, DeviceBackend, app::AppBackend};
use serde_json::{Value, json};
use std::{cell::RefCell, rc::Rc};
use uuid::Uuid;

#[derive(Default)]
struct State {
    devices: Vec<DeviceInfo>,
    attempts: Vec<(Option<DeviceId>, DeviceId)>,
    enumeration_failed: bool,
    catalog_reads: usize,
    runtime_reads: usize,
}
struct FakeBackend(Rc<RefCell<State>>, u128);
impl DeviceBackend for FakeBackend {
    fn plugin_catalog(&self) -> Value {
        self.0.borrow_mut().catalog_reads += 1;
        json!([])
    }
    fn plugin_runtime(&self) -> Value {
        let mut state = self.0.borrow_mut();
        state.runtime_reads += 1;
        json!({"test_tick": state.runtime_reads})
    }
    fn enumerate(&mut self) -> Result<Vec<DeviceInfo>, BackendError> {
        if self.0.borrow().enumeration_failed {
            return Err(BackendError::new(
                "Enumerar endpoints MMDevice",
                std::io::Error::other("0xE000020B"),
            ));
        }
        Ok(self.0.borrow().devices.clone())
    }
    fn new_id(&mut self) -> Result<Uuid, BackendError> {
        self.1 += 1;
        Ok(Uuid::from_u128(self.1))
    }
    fn start_audio(&mut self, config: AudioConfig) -> Result<AudioSession, BackendError> {
        self.0
            .borrow_mut()
            .attempts
            .push((config.capture, config.output));
        Err(BackendError::new(
            "fake offline",
            std::io::Error::other("no hardware"),
        ))
    }
}

#[test]
fn snapshot_reuses_structure_but_refreshes_runtime_and_tracks_suspended_edits() {
    let state = Rc::new(RefCell::new(State::default()));
    let mut app = AppBackend::new(FakeBackend(state.clone(), 0));
    let tone = Uuid::from_u128(1);
    let output = Uuid::from_u128(2);
    let missing = Uuid::from_u128(3);
    let project = json!({"format":"nodivu-project","version":1,
        "graph":{"schema_version":2,"nodes":[
            {"id":tone,"block":{"kind":"tone","enabled":true}},
            {"id":output,"block":{"kind":"output","endpoint_id":"offline"}},
            {"id":missing,"block":{"kind":"plugin","plugin_id":"missing","source":true,"parameters":{},"bypass":false}}
        ],"edges":[{"from":tone,"to":output}]},
        "positions":[{"node_id":tone,"x":0,"y":0},{"node_id":output,"x":100,"y":0},{"node_id":missing,"x":200,"y":0}],
        "camera":{"x":0,"y":0,"scale":1},"resources":[]});
    let loaded = call(
        &mut app,
        "project.open",
        json!({"expected_revision":0,"contents":project.to_string()}),
    )
    .unwrap();
    let snapshot = &loaded["snapshot"];
    assert_eq!(snapshot["signal"], 2);
    assert_eq!(snapshot["missing_plugins"][0]["node_id"], json!(missing));
    let reads = state.borrow().catalog_reads;
    let mut tick = snapshot["plugin_states"]["test_tick"].as_u64().unwrap();
    for _ in 0..100 {
        let s = app.snapshot();
        assert_eq!(s["signal"], 2);
        assert_eq!(s["missing_plugins"], snapshot["missing_plugins"]);
        let next = s["plugin_states"]["test_tick"].as_u64().unwrap();
        assert!(next > tick, "transport/metrics must remain live");
        tick = next;
    }
    assert_eq!(
        state.borrow().catalog_reads,
        reads,
        "polling must not rebuild the execution projection"
    );
    assert_eq!(
        call(&mut app, "audio.mute", json!({"muted":true})).unwrap()["signal"],
        0
    );
    assert_eq!(
        call(&mut app, "audio.mute", json!({"muted":false})).unwrap()["signal"],
        2
    );
    let mut graph = snapshot["graph"].clone();
    graph["edges"] = json!([]);
    graph["nodes"]
        .as_array_mut()
        .unwrap()
        .retain(|n| n["id"] != json!(missing));
    let edited = call(
        &mut app,
        "graph.apply",
        json!({"expected_revision":snapshot["revision"],"graph":graph}),
    )
    .unwrap();
    assert_eq!(edited["signal"], 0);
    assert_eq!(edited["missing_plugins"], json!([]));
    assert_eq!(edited["suspended"], true);
    assert!(state.borrow().attempts.is_empty());
}
fn call(
    app: &mut AppBackend<FakeBackend>,
    command: &str,
    params: Value,
) -> Result<Value, ApiError> {
    app.request(Request {
        protocol_version: 1,
        id: "test".into(),
        command: command.into(),
        params,
    })
}
fn device(id: &str, flow: Flow) -> DeviceInfo {
    DeviceInfo {
        endpoint_id: DeviceId(id.into()),
        name: "Same editable name".into(),
        flow,
        state: DeviceState::Active,
        hardware_kind: HardwareKind::Virtual,
        is_default: false,
        support: Support::Unknown,
        reason: None,
    }
}

#[test]
fn discovery_failure_preserves_editing_and_retry_does_not_replace_document() {
    let state = Rc::new(RefCell::new(State {
        enumeration_failed: true,
        ..State::default()
    }));
    let mut app = AppBackend::new(FakeBackend(Rc::clone(&state), 0));
    call(&mut app, "system.hello", json!({})).expect("backend alive");
    assert!(
        call(&mut app, "devices.list", json!({}))
            .expect_err("explicit failure")
            .message
            .contains("0xE000020B")
    );
    let s = call(
        &mut app,
        "node.add",
        json!({"expected_revision":0,"kind":"gain"}),
    )
    .expect("edit without devices");
    assert_eq!(s["graph"]["nodes"].as_array().expect("nodes").len(), 1);
    call(&mut app, "session.snapshot", json!({})).expect("state available");
    state.borrow_mut().enumeration_failed = false;
    state
        .borrow_mut()
        .devices
        .push(device("actual-id", Flow::Render));
    let inventory = call(&mut app, "devices.list", json!({})).expect("recovery");
    assert_eq!(inventory["devices"][0]["endpoint_id"], "actual-id");
    assert_eq!(app.snapshot()["graph"], s["graph"]);
    assert_eq!(app.snapshot()["revision"], s["revision"]);
    assert!(state.borrow().attempts.is_empty());
}

#[test]
fn virtual_selection_uses_ids_and_preserves_route_on_wrong_flow_missing_or_inactive() {
    let state = Rc::new(RefCell::new(State {
        devices: vec![
            device("vc-in", Flow::Render),
            device("vc-out", Flow::Capture),
        ],
        ..State::default()
    }));
    let mut app = AppBackend::new(FakeBackend(Rc::clone(&state), 0));
    let mut s = call(
        &mut app,
        "node.add",
        json!({"expected_revision":0,"kind":"capture"}),
    )
    .expect("capture");
    s = call(
        &mut app,
        "node.add",
        json!({"expected_revision":s["revision"],"kind":"output"}),
    )
    .expect("output");
    s["graph"]["nodes"][0]["block"]["endpoint_id"] = json!("vc-out");
    s["graph"]["nodes"][1]["block"]["endpoint_id"] = json!("vc-in");
    s = call(
        &mut app,
        "graph.apply",
        json!({"expected_revision":s["revision"],"graph":s["graph"]}),
    )
    .expect("virtual endpoints allowed");
    assert_eq!(s["generation"], 0, "fake must not claim live audio");
    assert_eq!(
        state.borrow().attempts,
        vec![(Some(DeviceId("vc-out".into())), DeviceId("vc-in".into()))]
    );
    state
        .borrow_mut()
        .devices
        .push(device("inactive", Flow::Render));
    state.borrow_mut().devices[2].state = DeviceState::Disabled;
    for invalid in ["vc-out", "missing", "inactive"] {
        let mut graph = s["graph"].clone();
        graph["nodes"][1]["block"]["endpoint_id"] = json!(invalid);
        assert_eq!(
            call(
                &mut app,
                "graph.apply",
                json!({"expected_revision":s["revision"],"graph":graph})
            )
            .expect_err("reject invalid selection")
            .code,
            ErrorCode::DeviceUnavailable
        );
        assert_eq!(app.snapshot()["graph"], s["graph"]);
        assert_eq!(app.snapshot()["revision"], s["revision"]);
    }
    // Removal does not force a replacement during unrelated edits or retry.
    state.borrow_mut().devices.clear();
    s = call(
        &mut app,
        "node.add",
        json!({"expected_revision":s["revision"],"kind":"gain"}),
    )
    .expect("edit with saved missing IDs");
    assert_eq!(s["graph"]["nodes"][1]["block"]["endpoint_id"], "vc-in");
    assert_eq!(state.borrow().attempts.len(), 1);
    call(&mut app, "audio.retry", json!({})).expect("retry reports hardware failure in snapshot");
    let state = state.borrow();
    assert_eq!(state.attempts.len(), 2);
    assert_eq!(state.attempts[0], state.attempts[1]);
}

#[test]
fn monitor_selection_does_not_edit_project_or_reopen_primary() {
    let state = Rc::new(RefCell::new(State {
        devices: vec![
            device("cable", Flow::Render),
            device("phones", Flow::Render),
            device("mic", Flow::Capture),
        ],
        ..State::default()
    }));
    let mut app = AppBackend::new(FakeBackend(state.clone(), 0));
    assert!(call(&mut app, "audio.monitor", json!({"endpoint_id":"phones"})).is_err());
    let mut s = call(
        &mut app,
        "node.add",
        json!({"expected_revision":0,"kind":"output"}),
    )
    .unwrap();
    s["graph"]["nodes"][0]["block"]["endpoint_id"] = json!("cable");
    s = call(
        &mut app,
        "graph.apply",
        json!({"expected_revision":s["revision"],"graph":s["graph"]}),
    )
    .unwrap();
    for id in ["cable", "mic", "missing"] {
        assert!(call(&mut app, "audio.monitor", json!({"endpoint_id":id})).is_err());
    }
    let monitored = call(&mut app, "audio.monitor", json!({"endpoint_id":"phones"})).unwrap();
    assert_eq!(monitored["monitor"]["endpoint_id"], "phones");
    assert_eq!(monitored["revision"], s["revision"]);
    assert_eq!(monitored["graph"], s["graph"]);
    call(&mut app, "audio.monitor", json!({"endpoint_id":null})).unwrap();
    assert_eq!(
        state.borrow().attempts.len(),
        1,
        "monitor control never restarts primary"
    );
}
