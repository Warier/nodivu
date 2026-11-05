//! Bounded fault state: one audio writer, control readers. No formatting/locks in DSP.
use nodivu_clap_host::Error;
use nodivu_core::graph::NodeId;
use std::sync::atomic::{AtomicI32, AtomicU64, Ordering::SeqCst};

#[derive(Default)]
pub struct FaultMailbox {
    sequence: AtomicU64,
    high: AtomicU64,
    low: AtomicU64,
    kind: AtomicI32,
    code: AtomicI32,
}
impl FaultMailbox {
    pub fn is_quarantined(&self) -> bool {
        self.kind.load(SeqCst) != 0
    }
    pub fn record(&self, id: NodeId, error: &Error) {
        let (kind, code) = match error {
            Error::ProcessStatus(code) => (1, *code),
            Error::Contract("Plugin produziu amostra não finita; saída silenciada") => (2, 0),
            Error::Contract(_) => (3, 0),
            Error::Windows(error) => (4, error.code().0),
            Error::Io(error) => (5, error.raw_os_error().unwrap_or(0)),
        };
        self.sequence.fetch_add(1, SeqCst);
        self.high.store((id.0.as_u128() >> 64) as u64, SeqCst);
        self.low.store(id.0.as_u128() as u64, SeqCst);
        self.code.store(code, SeqCst);
        self.kind.store(kind, SeqCst);
        self.sequence.fetch_add(1, SeqCst);
    }
    pub fn read(&self, id: NodeId) -> Option<(i32, i32)> {
        let sequence = self.sequence.load(SeqCst);
        if sequence & 1 != 0 {
            return None;
        }
        let stored = (u128::from(self.high.load(SeqCst)) << 64) | u128::from(self.low.load(SeqCst));
        let kind = self.kind.load(SeqCst);
        let code = self.code.load(SeqCst);
        (kind != 0 && stored == id.0.as_u128() && sequence == self.sequence.load(SeqCst))
            .then_some((kind, code))
    }
    /// Control thread only: allocate human-readable diagnostics outside DSP.
    pub fn snapshot(&self, id: NodeId) -> serde_json::Value {
        self.read(id).map_or(serde_json::Value::Null, |(kind, code)| {
            let reason = match kind {
                1 => "Plugin retornou erro de processamento",
                2 => "Plugin produziu áudio não finito (NaN/Infinity)",
                3 => "Plugin violou o contrato de processamento",
                4 => "Falha nativa durante processamento",
                _ => "Falha de I/O durante processamento",
            };
            serde_json::json!({"kind":kind,"code":code,"message":format!("{reason} (código {code}). Bloco silenciado; outros ramos continuam. Remova e recrie o bloco para usar outra instância, ou salve o projeto e reconecte o motor.")})
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn a_reused_slot_does_not_inherit_another_nodes_diagnostic() {
        let mailbox = FaultMailbox::default();
        let old = NodeId(uuid::Uuid::from_u128(1));
        let new = NodeId(uuid::Uuid::from_u128(2));
        mailbox.record(old, &Error::ProcessStatus(-9));
        assert_eq!(mailbox.read(old), Some((1, -9)));
        assert_eq!(mailbox.read(new), None);
        mailbox.record(
            new,
            &Error::Contract("Plugin produziu amostra não finita; saída silenciada"),
        );
        assert_eq!(mailbox.read(new), Some((2, 0)));
        assert_eq!(mailbox.read(old), None);
    }
}
