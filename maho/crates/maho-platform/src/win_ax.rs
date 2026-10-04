//! Windows accessibility inspection via UI Automation (CUIAutomation8 COM).
//! Walks top-level windows of the target PID depth-first (depth 8, cap 2000),
//! mirroring the macOS AX contract. Frontmost resolution uses the foreground
//! window's PID.

#![cfg(target_os = "windows")]

use crate::error::PlatformError;
use crate::grounding::{AppTarget, ScreenElement};
use windows::Win32::Foundation::RPC_E_CHANGED_MODE;
use windows::Win32::System::Com::{
    CoCreateInstance, CoInitializeEx, CoUninitialize, CLSCTX_INPROC_SERVER, COINIT_MULTITHREADED,
};
use windows::Win32::UI::Accessibility::{
    CUIAutomation8, IUIAutomation, IUIAutomationElement, IUIAutomationTreeWalker,
};

const MAX_DEPTH: usize = 8;
const CAP: usize = 2000;

pub fn read_elements(app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
    let pid = match app {
        AppTarget::Pid(p) => p,
        AppTarget::Frontmost => frontmost_pid()?,
    };
    unsafe {
        let initiated = CoInitializeEx(None, COINIT_MULTITHREADED);
        // S_FALSE (1, already initialized) and RPC_E_CHANGED_MODE (STA thread,
        // MTA objects still usable) are tolerable; anything else is fatal.
        if initiated.is_err() && initiated != RPC_E_CHANGED_MODE && initiated.0 != 1 {
            return Err(PlatformError::Io(format!(
                "CoInitializeEx failed: {}",
                initiated.0
            )));
        }
        struct ComApartment(bool);
        impl Drop for ComApartment {
            fn drop(&mut self) {
                if self.0 {
                    // SAFETY: balances this thread's successful CoInitializeEx.
                    unsafe { CoUninitialize() };
                }
            }
        }
        // S_OK and S_FALSE both acquire ownership; changed mode acquires none.
        // Declare before the interfaces so they are released before teardown.
        let _apartment = ComApartment(initiated.is_ok());
        let automation: IUIAutomation =
            CoCreateInstance(&CUIAutomation8, None, CLSCTX_INPROC_SERVER).map_err(com_err)?;
        let walker = automation.ControlViewWalker().map_err(com_err)?;
        let root = automation.GetRootElement().map_err(com_err)?;
        let mut out: Vec<ScreenElement> = Vec::new();
        let mut scanned_pids: Vec<u32> = Vec::new();
        // TreeWalker Err = no such child/sibling (S_OK+null or S_FALSE): treat
        // as end-of-iteration, never propagate. The SSH-session desktop root
        // legitimately has no children (session isolation).
        let mut top = match walker.GetFirstChildElement(&root) {
            Ok(t) => t,
            Err(_) => return Ok(out),
        };
        loop {
            let top_pid = top.CurrentProcessId().map_err(com_err)?;
            scanned_pids.push(top_pid as u32);
            if top_pid as i32 == pid {
                walk(&walker, &top, pid, 0, &mut out);
            } else {
                // Some apps nest their windows under a hidden proxy - probe
                // one level deep for children owned by the target process.
                let mut sub = match walker.GetFirstChildElement(&top) {
                    Ok(c) => c,
                    Err(_) => {
                        match walker.GetNextSiblingElement(&top) {
                            Ok(n) => top = n,
                            Err(_) => break,
                        }
                        continue;
                    }
                };
                loop {
                    let sub_pid = sub.CurrentProcessId().map_err(com_err)?;
                    if sub_pid as i32 == pid {
                        walk(&walker, &sub, pid, 0, &mut out);
                    }
                    match walker.GetNextSiblingElement(&sub) {
                        Ok(n) => sub = n,
                        Err(_) => break,
                    }
                }
            }
            match walker.GetNextSiblingElement(&top) {
                Ok(n) => top = n,
                Err(_) => break,
            }
        }
        if out.is_empty() {
            let summary = scanned_pids
                .iter()
                .map(|p| p.to_string())
                .collect::<Vec<_>>()
                .join(",");
            return Err(PlatformError::Io(format!(
                "UIA diagnostic: {} top-level windows, pids=[{}], target={}",
                scanned_pids.len(),
                summary,
                pid
            )));
        }
        Ok(out)
    }
}

unsafe fn walk(
    walker: &IUIAutomationTreeWalker,
    el: &IUIAutomationElement,
    pid: i32,
    depth: usize,
    out: &mut Vec<ScreenElement>,
) {
    if out.len() >= CAP || depth > MAX_DEPTH {
        return;
    }
    let role = el
        .CurrentLocalizedControlType()
        .map(|s| s.to_string())
        .unwrap_or_default();
    let label = el.CurrentName().map(|s| s.to_string()).unwrap_or_default();
    let rect = el.CurrentBoundingRectangle().unwrap_or_default();
    out.push(ScreenElement {
        role,
        label: (!label.is_empty()).then_some(label),
        x: rect.left as i32,
        y: rect.top as i32,
        w: (rect.right - rect.left).max(0) as u32,
        h: (rect.bottom - rect.top).max(0) as u32,
        pid,
    });
    let mut child = match walker.GetFirstChildElement(el) {
        Ok(c) => c,
        Err(_) => return,
    };
    loop {
        walk(walker, &child, pid, depth + 1, out);
        match walker.GetNextSiblingElement(&child) {
            Ok(next) => child = next,
            Err(_) => break,
        }
    }
}

fn frontmost_pid() -> Result<i32, PlatformError> {
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        GetForegroundWindow, GetWindowThreadProcessId,
    };
    unsafe {
        let hwnd = GetForegroundWindow();
        if hwnd.is_null() {
            return Err(PlatformError::Io("no foreground window".into()));
        }
        let mut pid: u32 = 0;
        GetWindowThreadProcessId(hwnd, &mut pid);
        if pid == 0 {
            return Err(PlatformError::Io(
                "foreground window pid unavailable".into(),
            ));
        }
        Ok(pid as i32)
    }
}

fn com_err(e: windows::core::Error) -> PlatformError {
    PlatformError::Io(format!("UI Automation error: {e}"))
}
