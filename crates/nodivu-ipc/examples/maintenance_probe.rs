//! Processo auxiliar de teste: stderr sinaliza manutenção mesmo com stdin incompleto.
use nodivu_core::Response;
use std::cell::Cell;
fn main() -> Result<(), nodivu_core::ApiError> {
    let ticks = Cell::new(0_u64);
    nodivu_ipc::serve_with_maintenance(
        |frame| {
            (
                Response::new(
                    None,
                    Ok(serde_json::json!({"ticks":ticks.get(), "frame_bytes":frame.len()})),
                ),
                true,
            )
        },
        || {
            ticks.set(ticks.get() + 1);
            if ticks.get() == 3 {
                eprintln!("MAINTENANCE_READY");
            }
            Ok(())
        },
    )
}
