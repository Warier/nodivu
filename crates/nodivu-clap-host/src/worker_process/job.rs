//! Win32 ownership guard. The non-inherited handle also closes on host process exit.
use std::{os::windows::io::AsRawHandle, process::Child};
use windows::{
    Win32::{
        Foundation::{CloseHandle, HANDLE},
        System::JobObjects::*,
    },
    core::PCWSTR,
};

pub(super) struct Job(HANDLE);
impl Job {
    pub fn attach(child: &Child) -> windows::core::Result<Self> {
        // SAFETY: unnamed non-inherited job; returned handle is uniquely owned here.
        let job = Self(unsafe { CreateJobObjectW(None, PCWSTR::null())? });
        let mut limits = JOBOBJECT_EXTENDED_LIMIT_INFORMATION::default();
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        // SAFETY: initialized struct/size, live job and borrowed live Child handle.
        // No breakaway flag; descendants created after assignment inherit this job.
        unsafe {
            SetInformationJobObject(
                job.0,
                JobObjectExtendedLimitInformation,
                (&limits as *const JOBOBJECT_EXTENDED_LIMIT_INFORMATION).cast(),
                std::mem::size_of_val(&limits) as u32,
            )?;
            AssignProcessToJobObject(job.0, HANDLE(child.as_raw_handle()))?;
        }
        Ok(job)
    }
}
impl Drop for Job {
    fn drop(&mut self) {
        // SAFETY: one owned handle, no other close; kills only processes in this job.
        let _ = unsafe { CloseHandle(self.0) };
    }
}
