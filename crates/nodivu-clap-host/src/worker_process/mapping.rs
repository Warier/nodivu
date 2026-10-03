//! Windows FFI. PCM ownership is transferred by the broker's request/reply.
use std::{marker::PhantomData, rc::Rc};
use windows::{
    Win32::{
        Foundation::{CloseHandle, HANDLE, INVALID_HANDLE_VALUE},
        System::Memory::*,
    },
    core::PCWSTR,
};
pub const FRAMES: usize = 480;
const BANK_BYTES: usize = FRAMES * 2 * 4;
const HEADER_BYTES: usize = 64;
pub const BYTES: usize = HEADER_BYTES + BANK_BYTES * 2;

pub struct Mapping {
    handle: HANDLE,
    view: MEMORY_MAPPED_VIEW_ADDRESS,
    _owner: PhantomData<Rc<()>>,
}
impl Mapping {
    pub fn create(name: &str) -> windows::core::Result<Self> {
        let name: Vec<u16> = name.encode_utf16().chain(Some(0)).collect();
        // SAFETY: pagefile mapping, bounded size, valid terminated name, current-user defaults.
        let handle = unsafe {
            CreateFileMappingW(
                INVALID_HANDLE_VALUE,
                None,
                PAGE_READWRITE,
                0,
                BYTES as u32,
                PCWSTR(name.as_ptr()),
            )?
        };
        // SAFETY: live mapping handle and matching bounded view size.
        let view = unsafe { MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, BYTES) };
        if view.Value.is_null() {
            let error = windows::core::Error::from_thread();
            // SAFETY: handle acquired above; no view exists.
            let _ = unsafe { CloseHandle(handle) };
            return Err(error);
        }
        let mapping = Self {
            handle,
            view,
            _owner: PhantomData,
        };
        let mut header = [0_u8; HEADER_BYTES];
        header[..8].copy_from_slice(b"NDVWRK01");
        for (i, value) in [1_u32, 48_000, 2, 480, 64, 3904].into_iter().enumerate() {
            header[8 + i * 4..12 + i * 4].copy_from_slice(&value.to_le_bytes());
        }
        // SAFETY: the peer has not been started yet; region is writable and large enough.
        unsafe {
            std::ptr::copy_nonoverlapping(header.as_ptr(), mapping.view.Value.cast(), header.len());
        }
        Ok(mapping)
    }
    /// Caller owns both banks before request publication. No borrowed slices escape.
    pub fn write_input(&mut self, channels: &[[f32; FRAMES]; 2]) {
        // SAFETY: exclusive broker phase; two full planar channels fit input bank.
        unsafe {
            std::ptr::copy_nonoverlapping(
                channels.as_ptr().cast::<u8>(),
                self.view.Value.cast::<u8>().add(HEADER_BYTES),
                BANK_BYTES,
            );
        }
    }
    /// Read only after matching reply, before sending the next request.
    pub fn read_output(&self) -> [[f32; FRAMES]; 2] {
        let mut channels = [[0.0; FRAMES]; 2];
        // SAFETY: conforming peer returned ownership by reply; output bank has BANK_BYTES.
        unsafe {
            std::ptr::copy_nonoverlapping(
                self.view.Value.cast::<u8>().add(HEADER_BYTES + BANK_BYTES),
                channels.as_mut_ptr().cast::<u8>(),
                BANK_BYTES,
            );
        }
        channels
    }
}
impl Drop for Mapping {
    fn drop(&mut self) {
        // SAFETY: owned view/handle, no Rust references escape and broker already reaped peer.
        unsafe {
            let _ = UnmapViewOfFile(self.view);
            let _ = CloseHandle(self.handle);
        }
    }
}
