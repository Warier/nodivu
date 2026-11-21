//! Owner/control bridge for file-bank/1. No mutex, file I/O or allocations in DSP.
use nodivu_clap_host::OfflinePlugin;
use serde_json::{Value, json};
use std::{collections::VecDeque, path::Path, sync::Mutex};

const SLOTS: usize = 10;
#[derive(Default)]
struct Slot {
    assigned: bool,
    file: String,
    status: i32,
    position: u32,
    duration: u32,
    error: String,
}
struct Command {
    slot: u8,
    action: String,
    path: Option<String>,
}
struct Bank {
    count: usize,
    reset: bool,
    slots: [Slot; SLOTS],
    commands: VecDeque<Command>,
}
pub struct Banks(Vec<Mutex<Bank>>);
impl Banks {
    pub fn new(owners: &[OfflinePlugin]) -> Self {
        Self(
            owners
                .iter()
                .map(|owner| {
                    Mutex::new(Bank {
                        count: owner.file_bank_slots().unwrap_or(0) as usize,
                        reset: false,
                        slots: std::array::from_fn(|_| Slot::default()),
                        commands: VecDeque::new(),
                    })
                })
                .collect(),
        )
    }
    pub fn reset(&self, index: usize) -> Result<(), &'static str> {
        let Some(bank) = self.0.get(index) else {
            return Ok(());
        };
        let mut bank = bank.lock().map_err(|_| "Banco indisponível")?;
        bank.reset = true;
        bank.commands.clear();
        bank.slots = std::array::from_fn(|_| Slot::default());
        Ok(())
    }
    pub fn command(
        &self,
        index: usize,
        slot: u8,
        action: &str,
        path: Option<&str>,
    ) -> Result<(), &'static str> {
        if !matches!(action, "load" | "clear" | "trigger" | "stop")
            || (action != "load" && path.is_some())
        {
            return Err("Ação/argumento inválido para o soundboard");
        }
        let path = if action == "load" {
            let value = path.ok_or("Escolha um arquivo para esta posição")?;
            let p = Path::new(value);
            // Do not introduce implicit remote file access for the new capability.
            if value.starts_with("\\\\")
                || value.starts_with("//")
                || value.len() > 4096
                || value.contains('\0')
                || !p.is_absolute()
                || !p.is_file()
                || !p.extension().is_some_and(|ext| {
                    ["mp3", "wav", "m4a"]
                        .iter()
                        .any(|e| ext.eq_ignore_ascii_case(e))
                })
                || !p.metadata().is_ok_and(|m| m.len() <= 32 * 1024 * 1024)
            {
                return Err("Use MP3, WAV ou M4A local de até 32 MiB e 60 segundos por posição");
            }
            Some(value.to_owned())
        } else {
            None
        };
        let bank = self.0.get(index).ok_or("Plugin sem banco de arquivos")?;
        let mut bank = bank.lock().map_err(|_| "Banco indisponível")?;
        if usize::from(slot) >= bank.count {
            return Err("Posição do soundboard fora do limite");
        }
        if bank.commands.len() >= 32 {
            return Err("Soundboard ocupado; tente novamente");
        }
        let state = &mut bank.slots[usize::from(slot)];
        if action == "trigger" && !(2..=5).contains(&state.status) {
            return Err("Áudio desta posição ainda não está pronto");
        }
        state.error.clear();
        if action == "load" || action == "clear" {
            state.assigned = true;
            state.file = path.clone().unwrap_or_default();
            state.status = if action == "load" { 1 } else { 0 };
            state.position = 0;
            state.duration = 0;
        }
        bank.commands.push_back(Command {
            slot,
            action: action.to_owned(),
            path,
        });
        Ok(())
    }
    pub fn snapshot(&self, index: usize) -> Value {
        let Some(bank) = self.0.get(index) else {
            return Value::Null;
        };
        let Ok(bank) = bank.lock() else {
            return json!({"error":"Banco indisponível"});
        };
        if bank.count == 0 {
            return Value::Null;
        }
        Value::Array(bank.slots[..bank.count].iter().enumerate().map(|(slot,s)| json!({"slot":slot,"assigned":s.assigned,"file":s.file,"status":s.status,"position_ms":s.position,"duration_ms":s.duration,"error":s.error})).collect())
    }
    pub fn maintain(&self, owners: &[OfflinePlugin]) {
        for (index, owner) in owners.iter().enumerate() {
            let Some(bank) = self.0.get(index) else {
                continue;
            };
            let Ok(mut bank) = bank.lock() else {
                continue;
            };
            if bank.count == 0 {
                continue;
            }
            if bank.reset {
                for slot in 0..bank.count {
                    let _ = owner.file_bank("clear", slot as u32, None);
                }
                bank.reset = false;
            }
            while let Some(command) = bank.commands.pop_front() {
                if let Err(error) = owner.file_bank(
                    &command.action,
                    u32::from(command.slot),
                    command.path.as_deref(),
                ) {
                    bank.slots[usize::from(command.slot)].error = error.to_string();
                }
            }
            for slot in 0..bank.count {
                match owner.file_bank("status", slot as u32, None) {
                    Ok((position, duration, status)) => {
                        let s = &mut bank.slots[slot];
                        s.position = position;
                        s.duration = duration;
                        s.status = status;
                    }
                    Err(error) => {
                        let s = &mut bank.slots[slot];
                        s.status = -1;
                        s.error = error.to_string();
                    }
                }
            }
        }
    }
}
