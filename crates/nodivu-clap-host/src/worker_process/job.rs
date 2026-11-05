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

/// Stable Rust Command does not expose the primary thread handle. A process
/// created suspended has not run its loader/user code yet; locate that thread
/// using its live child PID, resume it only after Job::attach succeeded.
pub(super) fn resume_primary_thread(child: &Child) -> windows::core::Result<()> {
    use std::os::windows::io::{FromRawHandle, OwnedHandle};
    use windows::Win32::{
        Foundation::E_UNEXPECTED,
        System::{Diagnostics::ToolHelp::*, Threading::*},
    };
    // SAFETY: owned snapshot, no borrowed references escape the function.
    let snapshot = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0)? };
    // SAFETY: successful snapshot handle is uniquely owned; transfer its close to RAII.
    let _snapshot_owner = unsafe { OwnedHandle::from_raw_handle(snapshot.0) };
    let mut entry = THREADENTRY32 {
        dwSize: std::mem::size_of::<THREADENTRY32>() as u32,
        ..Default::default()
    };
    // SAFETY: live snapshot and correctly sized, initialized entry.
    unsafe { Thread32First(snapshot, &mut entry)? };
    loop {
        if entry.th32OwnerProcessID == child.id() {
            // SAFETY: the child cannot exit/reuse PID while its suspended
            // primary thread is held. Request only suspend/resume rights.
            let thread = unsafe { OpenThread(THREAD_SUSPEND_RESUME, false, entry.th32ThreadID)? };
            // SAFETY: successful OpenThread handle has no other owning wrapper.
            let _thread_owner = unsafe { OwnedHandle::from_raw_handle(thread.0) };
            // SAFETY: our suspended child, already a member of the kill-on-close Job.
            if unsafe { ResumeThread(thread) } == u32::MAX {
                return Err(windows::core::Error::from_thread());
            }
            return Ok(());
        }
        // SAFETY: same owned snapshot/entry, no process is modified by enumeration.
        unsafe { Thread32Next(snapshot, &mut entry)? };
        if entry.dwSize < std::mem::size_of::<THREADENTRY32>() as u32 {
            return Err(E_UNEXPECTED.into());
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::{
        os::windows::process::CommandExt,
        process::{Command, Stdio},
        time::{Duration, Instant},
    };

    #[test]
    fn suspended_child() {
        let Some(marker) = std::env::var_os("NODIVU_JOB_TEST_MARKER") else {
            return;
        };
        std::fs::write(marker, b"user code started").unwrap();
        std::thread::sleep(Duration::from_secs(30));
    }

    #[test]
    fn child_cannot_execute_before_job_assignment_and_is_reaped_on_close() {
        let marker = std::env::temp_dir().join(format!("nodivu-job-{}.marker", std::process::id()));
        let _ = std::fs::remove_file(&marker);
        let mut child = Command::new(std::env::current_exe().unwrap())
            .args(["--exact", "worker_process::job::tests::suspended_child"])
            .env("NODIVU_JOB_TEST_MARKER", &marker)
            .stdin(Stdio::null())
            .stdout(Stdio::null())
            .stderr(Stdio::null())
            .creation_flags(0x0800_0004)
            .spawn()
            .unwrap();
        // Always reap the helper even when an assertion/assignment fails.
        let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
            std::thread::sleep(Duration::from_millis(30));
            assert!(!marker.exists());
            let job = Job::attach(&child).unwrap();
            resume_primary_thread(&child).unwrap();
            let start = Instant::now();
            while !marker.exists() && start.elapsed() < Duration::from_secs(5) {
                std::thread::sleep(Duration::from_millis(5));
            }
            assert!(marker.exists(), "child must run after assignment/resume");
            drop(job);
            let start = Instant::now();
            while child.try_wait().unwrap().is_none() && start.elapsed() < Duration::from_secs(5) {
                std::thread::sleep(Duration::from_millis(5));
            }
            assert!(
                child.try_wait().unwrap().is_some(),
                "closing Job must kill child"
            );
        }));
        let _ = child.kill();
        let _ = child.wait();
        let _ = std::fs::remove_file(marker);
        if let Err(error) = result {
            std::panic::resume_unwind(error);
        }
    }
}
