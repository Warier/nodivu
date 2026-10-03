//! Contrato do processo real. Não abre streams nem presume hardware disponível.
use serde_json::{Value, json};
use std::{
    io::Write,
    process::{Command, Stdio},
};
fn run(input: &[u8]) -> std::process::Output {
    let mut child = Command::new(env!("CARGO_BIN_EXE_nodivu-app-backend"))
        .env_remove("NODIVU_PLUGIN_MANIFEST")
        .env_remove("NODIVU_SCAN_PLUGINS")
        .stdin(Stdio::piped())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .expect("process");
    child
        .stdin
        .take()
        .expect("stdin")
        .write_all(input)
        .expect("input");
    child.wait_with_output().expect("exit")
}
fn request(command: &str, params: Value) -> String {
    json!({"protocol_version":1,"id":"ação 日本語","command":command,"params":params}).to_string()
}
#[test]
fn recovers_from_invalid_messages_and_accepts_final_line_without_newline() {
    let input = format!(
        "not-json\n{{\"id\":\"a\",\"id\":\"b\"}}\n{}\n{}\n{}",
        request("system.hello", json!({})),
        request("session.start", json!({})),
        request("session.snapshot", json!({}))
    );
    let out = run(input.as_bytes());
    assert!(out.status.success());
    assert!(out.stderr.is_empty());
    let rows: Vec<Value> = out
        .stdout
        .split(|&b| b == b'\n')
        .filter(|b| !b.is_empty())
        .map(|line| serde_json::from_slice(line).expect("only JSONL"))
        .collect();
    assert_eq!(rows.len(), 5);
    assert!(rows[0]["id"].is_null());
    assert_eq!(rows[1]["error"]["code"], "invalid_request");
    assert_eq!(rows[2]["id"], "ação 日本語");
    // Comando antigo deve falhar sem alterar a sessao.
    assert_eq!(rows[3]["error"]["code"], "invalid_request");
    assert_eq!(rows[4]["result"]["engine_state"], "idle");
    for row in rows {
        assert_ne!(row.get("result").is_some(), row.get("error").is_some());
    }
}
#[test]
fn shutdown_acknowledges_and_does_not_execute_next_request() {
    let input = format!(
        "{}\n{}\n",
        request("system.shutdown", json!({})),
        request("system.hello", json!({}))
    );
    let out = run(input.as_bytes());
    assert!(out.status.success());
    let value: Value = serde_json::from_slice(&out.stdout).expect("single response");
    assert_eq!(value["result"]["accepted"], true);
}
#[test]
fn rejects_utf8_version_unknown_fields_and_depth_then_recovers() {
    let mut input = b"\xff\n".to_vec();
    input.extend_from_slice(
        b"{\"protocol_version\":2,\"id\":\"v\",\"command\":\"system.hello\",\"params\":{}}\n",
    );
    input.extend_from_slice(
        format!("{}\n", request("system.hello", json!({"unknown":1}))).as_bytes(),
    );
    input.extend_from_slice(format!("{}0{}\n", "[".repeat(17), "]".repeat(17)).as_bytes());
    input.extend_from_slice(request("system.hello", json!({})).as_bytes());
    let out = run(&input);
    assert!(out.status.success());
    let rows: Vec<Value> = out
        .stdout
        .split(|&b| b == b'\n')
        .filter(|b| !b.is_empty())
        .map(|line| serde_json::from_slice(line).expect("JSONL"))
        .collect();
    assert_eq!(rows[1]["error"]["code"], "protocol_version_unsupported");
    for i in [0, 2, 3] {
        assert_eq!(rows[i]["error"]["code"], "invalid_request");
    }
    assert_eq!(rows[4]["ok"], true);
}
