//! Windows sandbox backend: AppContainer token + kill-on-close job object.
//!
//! A command runs inside a dedicated AppContainer with no capabilities, so it
//! has no network access and can write only where an ACL explicitly grants
//! the container SID. Only `policy.writable_roots` receive that grant (and it
//! is removed again when the run ends). The process is created suspended,
//! assigned to a job object that kills the whole process tree when the
//! handle closes, and only then resumed, so no descendant can escape.

use super::{PreparedSandbox, SandboxBackend, SandboxError, SandboxPolicy};
use std::ffi::OsStr;
use std::os::windows::ffi::OsStrExt;
use std::path::{Path, PathBuf};
use std::time::Duration;
use windows_sys::Win32::Foundation::{
    CloseHandle, LocalFree, GENERIC_ALL, HANDLE, INVALID_HANDLE_VALUE, WAIT_OBJECT_0, WAIT_TIMEOUT,
};
use windows_sys::Win32::Security::Authorization::{
    GetNamedSecurityInfoW, SetEntriesInAclW, SetNamedSecurityInfoW, EXPLICIT_ACCESS_W,
    GRANT_ACCESS, REVOKE_ACCESS, SE_FILE_OBJECT, TRUSTEE_IS_SID, TRUSTEE_IS_WELL_KNOWN_GROUP,
    TRUSTEE_W,
};
use windows_sys::Win32::Security::Isolation::{
    CreateAppContainerProfile, DeleteAppContainerProfile, DeriveAppContainerSidFromAppContainerName,
};
use windows_sys::Win32::Security::{
    FreeSid, ACL, DACL_SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, PSID, SECURITY_CAPABILITIES,
    SUB_CONTAINERS_AND_OBJECTS_INHERIT,
};
use windows_sys::Win32::System::JobObjects::{
    AssignProcessToJobObject, CreateJobObjectW, JobObjectExtendedLimitInformation,
    SetInformationJobObject, TerminateJobObject, JOBOBJECT_EXTENDED_LIMIT_INFORMATION,
    JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE,
};
use windows_sys::Win32::System::Threading::{
    CreateProcessW, DeleteProcThreadAttributeList, GetExitCodeProcess,
    InitializeProcThreadAttributeList, ResumeThread, UpdateProcThreadAttribute,
    WaitForSingleObject, CREATE_NO_WINDOW, CREATE_SUSPENDED, CREATE_UNICODE_ENVIRONMENT,
    EXTENDED_STARTUPINFO_PRESENT, LPPROC_THREAD_ATTRIBUTE_LIST, PROCESS_INFORMATION,
    PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, STARTUPINFOEXW,
};

/// HRESULT for "already exists" (HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)).
const HRESULT_ALREADY_EXISTS: i32 = 0x800700B7u32 as i32;

pub struct WindowsSandbox;

impl SandboxBackend for WindowsSandbox {
    fn prepare(&self, policy: &SandboxPolicy) -> Result<PreparedSandbox, SandboxError> {
        forced_unavailable()?;
        validate(policy)?;
        Ok(PreparedSandbox {})
    }
}

fn forced_unavailable() -> Result<(), SandboxError> {
    #[cfg(feature = "debug-test-hooks")]
    if std::env::var_os("MAHO_SANDBOX_FORCE_UNAVAILABLE").as_deref() == Some(OsStr::new("1")) {
        return Err(SandboxError::Unavailable(
            "forced by MAHO_SANDBOX_FORCE_UNAVAILABLE".into(),
        ));
    }
    Ok(())
}

fn validate(policy: &SandboxPolicy) -> Result<(), SandboxError> {
    if policy.network {
        // An AppContainer gets network access only through capabilities; this
        // backend grants none, so a network-enabled policy cannot be honoured.
        return Err(SandboxError::Unavailable(
            "network-enabled policies are not supported by the Windows backend".into(),
        ));
    }
    for root in &policy.writable_roots {
        if !root.is_absolute() || !root.is_dir() {
            return Err(SandboxError::Prepare(format!(
                "writable root must be an existing absolute directory: {}",
                root.display()
            )));
        }
    }
    for denied in &policy.denied_paths {
        if policy
            .writable_roots
            .iter()
            .any(|root| denied.starts_with(root) || root.starts_with(denied))
        {
            return Err(SandboxError::Prepare(format!(
                "denied path overlaps a writable root: {}",
                denied.display()
            )));
        }
    }
    Ok(())
}

#[derive(Debug)]
pub struct ConfinedExit {
    pub exit_code: u32,
    pub timed_out: bool,
}

/// Runs `command_line` (a full Windows command line, e.g. `cmd.exe /c ...`)
/// inside a fresh AppContainer with only `policy.writable_roots` writable,
/// waits up to `timeout`, and kills the whole process tree when it returns.
///
/// Fails closed: if any confinement step fails, nothing is spawned (or the
/// suspended process is terminated before it runs) and an error is returned.
pub fn run_confined(
    command_line: &str,
    cwd: &Path,
    policy: &SandboxPolicy,
    timeout: Duration,
) -> Result<ConfinedExit, SandboxError> {
    forced_unavailable()?;
    validate(policy)?;

    let name = format!(
        "maho.agent.sandbox.{}.{}",
        std::process::id(),
        std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_nanos())
            .unwrap_or_default()
    );
    let container = AppContainer::create(&name)?;
    let mut grants = Vec::new();
    for root in &policy.writable_roots {
        grants.push(AclGrant::grant(root, container.sid)?);
    }

    let job = Job::create()?;
    let process = spawn_suspended(command_line, cwd, container.sid)?;
    // SAFETY: both handles are valid; the process is suspended, so it cannot
    // run or spawn children before it is inside the job.
    if unsafe { AssignProcessToJobObject(job.0, process.process.0) } == 0 {
        // SAFETY: valid process handle; terminate the never-resumed process.
        unsafe { windows_sys::Win32::System::Threading::TerminateProcess(process.process.0, 1) };
        return Err(SandboxError::Unavailable(format!(
            "AssignProcessToJobObject failed: {}",
            std::io::Error::last_os_error()
        )));
    }
    // SAFETY: valid suspended primary thread handle.
    if unsafe { ResumeThread(process.thread.0) } == u32::MAX {
        // SAFETY: valid job handle; kills the suspended process.
        unsafe { TerminateJobObject(job.0, 1) };
        return Err(SandboxError::Unavailable(format!(
            "ResumeThread failed: {}",
            std::io::Error::last_os_error()
        )));
    }

    let wait_ms = u32::try_from(timeout.as_millis()).unwrap_or(u32::MAX);
    // SAFETY: valid process handle.
    let waited = unsafe { WaitForSingleObject(process.process.0, wait_ms) };
    let timed_out = waited == WAIT_TIMEOUT;
    if waited != WAIT_OBJECT_0 && !timed_out {
        // SAFETY: valid job handle.
        unsafe { TerminateJobObject(job.0, 1) };
        return Err(SandboxError::Prepare(format!(
            "waiting for the confined process failed: {}",
            std::io::Error::last_os_error()
        )));
    }
    let mut exit_code = 0u32;
    if timed_out {
        // SAFETY: valid job handle; kills the process and every descendant.
        unsafe { TerminateJobObject(job.0, 1) };
        exit_code = 1;
    } else {
        // SAFETY: valid process handle and out-pointer.
        unsafe { GetExitCodeProcess(process.process.0, &mut exit_code) };
    }
    drop(process);
    drop(job); // kill-on-close: any descendant still running dies here.
    drop(grants);
    drop(container);
    Ok(ConfinedExit {
        exit_code,
        timed_out,
    })
}

fn wide(value: &OsStr) -> Vec<u16> {
    value.encode_wide().chain(std::iter::once(0)).collect()
}

fn last_error(context: &str) -> SandboxError {
    SandboxError::Unavailable(format!("{context}: {}", std::io::Error::last_os_error()))
}

struct OwnedHandle(HANDLE);

impl Drop for OwnedHandle {
    fn drop(&mut self) {
        if !self.0.is_null() && self.0 != INVALID_HANDLE_VALUE {
            // SAFETY: the handle is owned by this wrapper and closed once.
            unsafe { CloseHandle(self.0) };
        }
    }
}

struct Job(HANDLE);

impl Job {
    fn create() -> Result<Self, SandboxError> {
        // SAFETY: null attributes and name create an anonymous job object.
        let handle = unsafe { CreateJobObjectW(std::ptr::null(), std::ptr::null()) };
        if handle.is_null() {
            return Err(last_error("CreateJobObjectW failed"));
        }
        let job = Job(handle);
        // SAFETY: zero is a valid initial value for this plain-data struct.
        let mut limits: JOBOBJECT_EXTENDED_LIMIT_INFORMATION = unsafe { std::mem::zeroed() };
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        // SAFETY: valid job handle and a correctly sized limit struct.
        let ok = unsafe {
            SetInformationJobObject(
                job.0,
                JobObjectExtendedLimitInformation,
                (&limits as *const JOBOBJECT_EXTENDED_LIMIT_INFORMATION).cast(),
                std::mem::size_of::<JOBOBJECT_EXTENDED_LIMIT_INFORMATION>() as u32,
            )
        };
        if ok == 0 {
            return Err(last_error("SetInformationJobObject failed"));
        }
        Ok(job)
    }
}

impl Drop for Job {
    fn drop(&mut self) {
        // SAFETY: owned job handle; closing it kills the remaining tree.
        unsafe { CloseHandle(self.0) };
    }
}

struct AppContainer {
    name: Vec<u16>,
    sid: PSID,
}

impl AppContainer {
    fn create(name: &str) -> Result<Self, SandboxError> {
        let name_w = wide(OsStr::new(name));
        let display = wide(OsStr::new("Maho agent sandbox"));
        let mut sid: PSID = std::ptr::null_mut();
        // SAFETY: valid NUL-terminated strings; no capabilities are granted.
        let hr = unsafe {
            CreateAppContainerProfile(
                name_w.as_ptr(),
                display.as_ptr(),
                display.as_ptr(),
                std::ptr::null(),
                0,
                &mut sid,
            )
        };
        if hr == HRESULT_ALREADY_EXISTS {
            // SAFETY: valid name; out-pointer receives a SID we free below.
            let hr =
                unsafe { DeriveAppContainerSidFromAppContainerName(name_w.as_ptr(), &mut sid) };
            if hr < 0 {
                return Err(SandboxError::Unavailable(format!(
                    "DeriveAppContainerSidFromAppContainerName failed: {hr:#x}"
                )));
            }
        } else if hr < 0 {
            return Err(SandboxError::Unavailable(format!(
                "CreateAppContainerProfile failed: {hr:#x}"
            )));
        }
        Ok(AppContainer { name: name_w, sid })
    }
}

impl Drop for AppContainer {
    fn drop(&mut self) {
        // SAFETY: the SID came from the AppContainer APIs (FreeSid frees it);
        // the profile name is NUL-terminated.
        unsafe {
            FreeSid(self.sid);
            DeleteAppContainerProfile(self.name.as_ptr());
        }
    }
}

/// Grants the container SID full access to a directory tree and revokes the
/// grant on drop.
struct AclGrant {
    path: Vec<u16>,
    sid: PSID,
}

impl AclGrant {
    fn grant(path: &Path, sid: PSID) -> Result<Self, SandboxError> {
        let path_w = wide(path.as_os_str());
        set_access(&path_w, sid, GRANT_ACCESS)?;
        Ok(AclGrant { path: path_w, sid })
    }
}

impl Drop for AclGrant {
    fn drop(&mut self) {
        // Best effort on cleanup: a failure here leaves an extra ACE for a
        // container SID that no longer exists, which grants nothing.
        let _ = set_access(&self.path, self.sid, REVOKE_ACCESS);
    }
}

fn set_access(path: &[u16], sid: PSID, mode: i32) -> Result<(), SandboxError> {
    let mut old_dacl: *mut ACL = std::ptr::null_mut();
    let mut descriptor: PSECURITY_DESCRIPTOR = std::ptr::null_mut();
    // SAFETY: valid path; out-pointers receive a DACL inside `descriptor`.
    let status = unsafe {
        GetNamedSecurityInfoW(
            path.as_ptr(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            &mut old_dacl,
            std::ptr::null_mut(),
            &mut descriptor,
        )
    };
    if status != 0 {
        return Err(SandboxError::Prepare(format!(
            "GetNamedSecurityInfoW failed: {status}"
        )));
    }
    let entry = EXPLICIT_ACCESS_W {
        grfAccessPermissions: GENERIC_ALL,
        grfAccessMode: mode,
        grfInheritance: SUB_CONTAINERS_AND_OBJECTS_INHERIT,
        Trustee: TRUSTEE_W {
            pMultipleTrustee: std::ptr::null_mut(),
            MultipleTrusteeOperation: 0,
            TrusteeForm: TRUSTEE_IS_SID,
            TrusteeType: TRUSTEE_IS_WELL_KNOWN_GROUP,
            ptstrName: sid.cast(),
        },
    };
    let mut new_dacl: *mut ACL = std::ptr::null_mut();
    // SAFETY: one valid entry; `old_dacl` stays valid until `descriptor` is freed.
    let status = unsafe { SetEntriesInAclW(1, &entry, old_dacl, &mut new_dacl) };
    if status != 0 {
        // SAFETY: descriptor was allocated by GetNamedSecurityInfoW.
        unsafe { LocalFree(descriptor) };
        return Err(SandboxError::Prepare(format!(
            "SetEntriesInAclW failed: {status}"
        )));
    }
    // SAFETY: valid path and freshly built DACL.
    let status = unsafe {
        SetNamedSecurityInfoW(
            path.as_ptr(),
            SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            new_dacl,
            std::ptr::null(),
        )
    };
    // SAFETY: both were allocated by the security APIs above.
    unsafe {
        LocalFree(new_dacl.cast());
        LocalFree(descriptor);
    }
    if status != 0 {
        return Err(SandboxError::Prepare(format!(
            "SetNamedSecurityInfoW failed: {status}"
        )));
    }
    Ok(())
}

struct SuspendedProcess {
    process: OwnedHandle,
    thread: OwnedHandle,
}

fn spawn_suspended(
    command_line: &str,
    cwd: &Path,
    container_sid: PSID,
) -> Result<SuspendedProcess, SandboxError> {
    let mut capabilities = SECURITY_CAPABILITIES {
        AppContainerSid: container_sid,
        Capabilities: std::ptr::null_mut(),
        CapabilityCount: 0,
        Reserved: 0,
    };
    let mut size = 0usize;
    // SAFETY: a null list with a size out-pointer queries the required size.
    unsafe { InitializeProcThreadAttributeList(std::ptr::null_mut(), 1, 0, &mut size) };
    let mut buffer = vec![0u8; size];
    let list: LPPROC_THREAD_ATTRIBUTE_LIST = buffer.as_mut_ptr().cast();
    // SAFETY: `buffer` has the size the first call reported.
    if unsafe { InitializeProcThreadAttributeList(list, 1, 0, &mut size) } == 0 {
        return Err(last_error("InitializeProcThreadAttributeList failed"));
    }
    struct ListGuard(LPPROC_THREAD_ATTRIBUTE_LIST);
    impl Drop for ListGuard {
        fn drop(&mut self) {
            // SAFETY: the list was initialized above.
            unsafe { DeleteProcThreadAttributeList(self.0) };
        }
    }
    let _guard = ListGuard(list);
    // SAFETY: `capabilities` outlives CreateProcessW below.
    let ok = unsafe {
        UpdateProcThreadAttribute(
            list,
            0,
            PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES as usize,
            (&mut capabilities as *mut SECURITY_CAPABILITIES).cast(),
            std::mem::size_of::<SECURITY_CAPABILITIES>(),
            std::ptr::null_mut(),
            std::ptr::null(),
        )
    };
    if ok == 0 {
        return Err(last_error("UpdateProcThreadAttribute failed"));
    }

    // SAFETY: zero is a valid initial value for these plain-data structs.
    let mut startup: STARTUPINFOEXW = unsafe { std::mem::zeroed() };
    startup.StartupInfo.cb = std::mem::size_of::<STARTUPINFOEXW>() as u32;
    startup.lpAttributeList = list;
    // SAFETY: as above.
    let mut info: PROCESS_INFORMATION = unsafe { std::mem::zeroed() };
    let mut command = wide(OsStr::new(command_line));
    let cwd_w = wide(PathBuf::from(cwd).as_os_str());
    // SAFETY: command is a mutable NUL-terminated buffer; all pointers are valid.
    let ok = unsafe {
        CreateProcessW(
            std::ptr::null(),
            command.as_mut_ptr(),
            std::ptr::null(),
            std::ptr::null(),
            0,
            EXTENDED_STARTUPINFO_PRESENT
                | CREATE_SUSPENDED
                | CREATE_UNICODE_ENVIRONMENT
                | CREATE_NO_WINDOW,
            std::ptr::null(),
            cwd_w.as_ptr(),
            &startup.StartupInfo,
            &mut info,
        )
    };
    if ok == 0 {
        return Err(last_error("CreateProcessW in AppContainer failed"));
    }
    Ok(SuspendedProcess {
        process: OwnedHandle(info.hProcess),
        thread: OwnedHandle(info.hThread),
    })
}
