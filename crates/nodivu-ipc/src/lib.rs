use nodivu_core::*;
use std::{
    io::{self, BufRead, Write},
    sync::mpsc,
    thread,
    time::{Duration, Instant},
};

pub fn io_error(e: io::Error) -> ApiError {
    ApiError::new(
        ErrorCode::InternalError,
        format!("Falha de transporte: {e}"),
    )
}

/// Limita ANTES de copiar. Excesso encerra sem drenar uma entrada infinita.
pub fn read_frame(reader: &mut impl BufRead) -> Result<Option<Vec<u8>>, ApiError> {
    let mut frame = Vec::new();
    loop {
        let buf = reader.fill_buf().map_err(io_error)?;
        if buf.is_empty() {
            return Ok(if frame.is_empty() { None } else { Some(frame) });
        }
        let newline = buf.iter().position(|&b| b == b'\n');
        let count = newline.map_or(buf.len(), |index| index + 1);
        if count > MAX_LINE_BYTES - frame.len() {
            return Err(ApiError::new(
                ErrorCode::ResourceLimit,
                "Linha excede 256 KiB; conexão encerrada.",
            ));
        }
        frame.extend_from_slice(&buf[..count]);
        reader.consume(count);
        if newline.is_some() {
            return Ok(Some(frame));
        }
    }
}

pub fn encode(response: Response) -> Result<Vec<u8>, ApiError> {
    let mut bytes = serde_json::to_vec(&response)
        .map_err(|e| ApiError::new(ErrorCode::InternalError, e.to_string()))?;
    if bytes.len() >= MAX_LINE_BYTES {
        bytes = serde_json::to_vec(&Response::new(
            response.id(),
            Err(ApiError::new(
                ErrorCode::ResourceLimit,
                "Resposta excede o limite de bytes.",
            )),
        ))
        .map_err(|e| ApiError::new(ErrorCode::InternalError, e.to_string()))?;
    }
    bytes.push(b'\n');
    Ok(bytes)
}

pub fn serve(handle: impl FnMut(&[u8]) -> (Response, bool)) -> Result<(), ApiError> {
    serve_with_maintenance(handle, || Ok(()))
}

/// Servidor de processo: stdin/stdout isolados; controle e manutenção no thread chamador.
/// Reader bloqueado em stdin é liberado pelo fechamento do processo após o retorno.
/// Não usar como servidor reiniciável dentro de um processo que continua vivo.
pub fn serve_with_maintenance(
    mut handle: impl FnMut(&[u8]) -> (Response, bool),
    mut maintenance: impl FnMut() -> Result<(), ApiError>,
) -> Result<(), ApiError> {
    let (input_send, input_receive) = mpsc::sync_channel(1);
    let _reader = thread::Builder::new()
        .name("jsonl-input".into())
        .spawn(move || {
            let mut input = io::stdin().lock();
            loop {
                let frame = read_frame(&mut input);
                let terminal = !matches!(frame, Ok(Some(_)));
                if input_send.send(frame).is_err() || terminal {
                    break;
                }
            }
        })
        .map_err(io_error)?;
    let (send, receive) = mpsc::sync_channel::<Vec<u8>>(1);
    let (ack_send, ack_receive) = mpsc::sync_channel(1);
    // O writer possui apenas stdout, nunca controlador ou recursos do backend.
    // Em falha/timeout o main retorna e o processo encerra todas as suas threads.
    // O chamador encerra AudioSession em EOF, erro de transporte ou timeout.
    let writer = thread::Builder::new()
        .name("jsonl-output".into())
        .spawn(move || {
            let mut out = io::stdout().lock();
            for bytes in receive {
                let result = out.write_all(&bytes).and_then(|()| out.flush());
                let failed = result.is_err();
                if ack_send.send(result).is_err() || failed {
                    break;
                }
            }
        })
        .map_err(io_error)?;
    loop {
        let mut shutdown = false;
        let (response, terminal_error) =
            match wait_maintained(&input_receive, None, &mut maintenance)? {
                Ok(Some(frame)) => {
                    let (response, done) = handle(&frame);
                    shutdown = done;
                    (response, None)
                }
                Ok(None) => break,
                Err(error) => (Response::new(None, Err(error.clone())), Some(error)),
            };
        send.send(encode(response)?)
            .map_err(|_| ApiError::new(ErrorCode::InternalError, "Writer encerrado."))?;
        wait_maintained(
            &ack_receive,
            Some(Instant::now() + Duration::from_secs(5)),
            &mut maintenance,
        )?
        .map_err(io_error)?;
        if let Some(error) = terminal_error {
            return Err(error);
        }
        if shutdown {
            break;
        }
    }
    drop(send);
    writer
        .join()
        .map_err(|_| ApiError::new(ErrorCode::InternalError, "Falha na thread de saída."))?;
    Ok(())
}

fn wait_maintained<T>(
    receiver: &mpsc::Receiver<T>,
    deadline: Option<Instant>,
    maintenance: &mut impl FnMut() -> Result<(), ApiError>,
) -> Result<T, ApiError> {
    loop {
        // Também atende com dados já disponíveis: fluxo contínuo de comandos não causa starvation.
        maintenance()?;
        let mut quantum = Duration::from_millis(10);
        if let Some(deadline) = deadline {
            let remaining = deadline.saturating_duration_since(Instant::now());
            if remaining.is_zero() {
                return Err(ApiError::new(
                    ErrorCode::InternalError,
                    "Cliente sem progresso no stdout por 5 s; encerrando processo.",
                ));
            }
            quantum = quantum.min(remaining);
        }
        match receiver.recv_timeout(quantum) {
            Ok(value) => return Ok(value),
            Err(mpsc::RecvTimeoutError::Timeout) => {}
            Err(mpsc::RecvTimeoutError::Disconnected) => {
                return Err(ApiError::new(
                    ErrorCode::InternalError,
                    "Thread de transporte encerrada sem resposta.",
                ));
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{BufReader, Cursor};
    #[test]
    fn maintenance_runs_without_input_and_with_pending_input() {
        let (send, receive) = mpsc::sync_channel(1);
        let mut ticks = 0;
        let value = wait_maintained(
            &receive,
            Some(Instant::now() + Duration::from_secs(1)),
            &mut || {
                ticks += 1;
                if ticks == 3 {
                    send.try_send(7).unwrap();
                }
                Ok(())
            },
        )
        .unwrap();
        assert_eq!(value, 7);
        assert_eq!(ticks, 3);
        send.send(9).unwrap();
        assert_eq!(
            wait_maintained(&receive, None, &mut || {
                ticks += 1;
                Ok(())
            })
            .unwrap(),
            9
        );
        assert_eq!(ticks, 4);
    }
    #[test]
    fn maintenance_error_and_output_deadline_are_terminal() {
        let (_send, receive) = mpsc::sync_channel::<()>(1);
        assert!(wait_maintained(&receive, Some(Instant::now()), &mut || Ok(())).is_err());
        let error = wait_maintained(&receive, None, &mut || {
            Err(ApiError::invalid("callback failed"))
        })
        .unwrap_err();
        assert!(error.to_string().contains("callback failed"));
    }
    #[test]
    fn framing_boundaries_and_final_line() {
        for size in [MAX_LINE_BYTES - 1, MAX_LINE_BYTES, MAX_LINE_BYTES + 1] {
            let mut data = vec![b' '; size - 1];
            data.push(b'\n');
            let mut reader = BufReader::with_capacity(7, Cursor::new(data));
            assert_eq!(read_frame(&mut reader).is_ok(), size <= MAX_LINE_BYTES);
        }
        let mut input = Cursor::new(b"{}\r\n{}");
        assert_eq!(
            read_frame(&mut input).expect("frame"),
            Some(b"{}\r\n".to_vec())
        );
        assert_eq!(
            read_frame(&mut input).expect("final frame"),
            Some(b"{}".to_vec())
        );
        assert!(read_frame(&mut input).expect("EOF").is_none());
    }
    #[test]
    fn huge_result_returns_bounded_error_with_same_id() {
        let response = Response::new(
            Some("a".into()),
            Ok(serde_json::json!({"x": "a".repeat(MAX_LINE_BYTES)})),
        );
        let bytes = encode(response).expect("encoding");
        let value: serde_json::Value = serde_json::from_slice(&bytes).expect("JSON");
        assert_eq!(value["id"], "a");
        assert_eq!(value["error"]["code"], "resource_limit");
        assert!(bytes.len() < MAX_LINE_BYTES);
    }
}
