//! Accessibility (AX) tree inspection capability (`maho desktop elements`).

use crate::error::PlatformError;
#[cfg(target_os = "macos")]
use crate::error::DEEP_LINK_ACCESSIBILITY;
use crate::grounding::{AppTarget, ScreenElement};

/// Maximum tree traversal depth to prevent infinite recursion on circular or deep structures.
pub const MAX_TRAVERSAL_DEPTH: usize = 8;

/// Maximum number of elements to collect across the entire accessibility tree.
pub const MAX_ELEMENT_CAP: usize = 2000;

/// Filter a slice of screen elements by role name (case-insensitive and prefix-tolerant).
pub fn filter_by_role(elements: &[ScreenElement], role: &str) -> Vec<ScreenElement> {
    elements
        .iter()
        .filter(|e| {
            e.role.eq_ignore_ascii_case(role)
                || e.role
                    .strip_prefix("AX")
                    .unwrap_or(&e.role)
                    .eq_ignore_ascii_case(role.strip_prefix("AX").unwrap_or(role))
        })
        .cloned()
        .collect()
}

/// Non-prompting Accessibility TCC probe (`AXIsProcessTrusted`).
pub fn accessibility_trusted() -> bool {
    #[cfg(target_os = "macos")]
    {
        // SAFETY: stateless framework query; does not prompt user for permissions.
        unsafe { AXIsProcessTrusted() != 0 }
    }
    #[cfg(not(target_os = "macos"))]
    {
        true
    }
}

/// Read accessibility elements from the specified application target.
///
/// Returns a list of UI elements with role, label, top-left pixel bounds, and process ID.
///
/// Traversal is depth-first with a depth limit of 8 and a cap of 2000 elements.
/// Partial results are returned if a subtree cannot be inspected.
#[cfg(target_os = "macos")]
pub fn read_elements(app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
    if !accessibility_trusted() {
        return Err(PlatformError::PermissionRequired {
            scope: "accessibility".to_string(),
            deep_link: DEEP_LINK_ACCESSIBILITY.to_string(),
        });
    }

    let pid = match app {
        AppTarget::Pid(pid) => pid,
        AppTarget::Frontmost => frontmost_app_pid()?,
    };

    // SAFETY: AXUIElementCreateApplication creates a +1 owned AXUIElementRef for the pid.
    let app_element = unsafe { AXUIElementCreateApplication(pid) };
    if app_element.is_null() {
        return Err(PlatformError::Io(format!(
            "failed to create accessibility element for process ID {pid}"
        )));
    }

    let attrs = match AxAttributes::new() {
        Some(attrs) => attrs,
        None => {
            // SAFETY: Releasing app_element on attribute allocation failure.
            unsafe {
                CFRelease(app_element as CFTypeRef);
            }
            return Err(PlatformError::Io(
                "failed to allocate accessibility attribute CFStrings".to_string(),
            ));
        }
    };

    let mut elements = Vec::new();
    traverse_element(app_element, pid, 0, &attrs, &mut elements);

    // SAFETY: app_element was allocated with AXUIElementCreateApplication (+1 retain count) and is released here.
    unsafe {
        CFRelease(app_element as CFTypeRef);
    }

    Ok(elements)
}

#[cfg(target_os = "windows")]
pub fn read_elements(app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
    crate::win_ax::read_elements(app)
}

#[cfg(not(any(target_os = "macos", target_os = "linux", target_os = "windows")))]
pub fn read_elements(_app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
    let _ = _app;
    Err(PlatformError::Unsupported {
        platform: std::env::consts::OS.to_string(),
        capability: "desktop.elements".to_string(),
        remediation:
            "accessibility tree element inspection is not implemented on this platform yet"
                .to_string(),
    })
}

#[cfg(target_os = "linux")]
pub fn read_elements(app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
    linux::read_elements(app)
}

#[cfg(target_os = "linux")]
mod linux {
    use super::*;
    use crate::probe::{CommandRunner, SystemRunner};

    const ATSPI_SCRIPT: &str = r#"import json, sys
pid = int(sys.argv[1])
import gi
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi
MAX_DEPTH, CAP, MAX_CHILDREN = 8, 2000, 200
out = []
def walk(node, depth):
    if len(out) >= CAP or depth > MAX_DEPTH:
        return
    try:
        role = node.get_role_name()
        name = node.get_name() or ""
    except Exception:
        return
    x = y = w = h = 0
    try:
        e = node.query_component().get_extents(Atspi.CoordType.SCREEN)
        x, y, w, h = e.x, e.y, e.width, e.height
    except Exception:
        pass
    out.append({"role": role, "label": name, "x": x, "y": y, "w": max(w, 0), "h": max(h, 0), "pid": pid})
    try:
        n = node.get_child_count()
    except Exception:
        return
    for i in range(min(n, MAX_CHILDREN)):
        try:
            walk(node.get_child_at_index(i), depth + 1)
        except Exception:
            break
desktop = Atspi.get_desktop()
for ai in range(desktop.get_child_count()):
    app = desktop.get_child_at_index(ai)
    try:
        if app.get_process_id() != pid:
            continue
    except Exception:
        continue
    walk(app, 0)
print(json.dumps(out))
"#;

    pub(super) fn read_elements(app: AppTarget) -> Result<Vec<ScreenElement>, PlatformError> {
        let runner = SystemRunner;
        let chain =
            crate::backends::probe_chain("linux", crate::backends::GroundingCapability::Elements);
        if !chain.contains(&"python3:gi-atspi") {
            return Err(PlatformError::Unsupported {
                platform: "linux".to_string(),
                capability: "desktop.elements".to_string(),
                remediation: "no accessibility backend registered".to_string(),
            });
        }
        let pid = match app {
            AppTarget::Pid(p) => p,
            AppTarget::Frontmost => frontmost_pid()?,
        };
        let Some(program) = runner.which("python3") else {
            return Err(PlatformError::Unsupported {
                platform: "linux".to_string(),
                capability: "desktop.elements".to_string(),
                remediation: "python3 is required for AT-SPI element inspection".to_string(),
            });
        };
        let gi_probe = runner.run(
            &program.to_string_lossy(),
            &["-c", "import gi\nimport gi.repository.Atspi"],
        );
        if !gi_probe.as_ref().map(|o| o.status == 0).unwrap_or(false) {
            return Err(PlatformError::Unsupported {
                platform: "linux".to_string(),
                capability: "desktop.elements".to_string(),
                remediation: "install python3-gi and gir1.2-atspi-2.0 for AT-SPI inspection"
                    .to_string(),
            });
        }
        let args = crate::linux_probe::atspi_args(pid);
        let args: Vec<&str> = args.iter().map(String::as_str).collect();
        let out = runner.run_with_stdin(&program.to_string_lossy(), &args, ATSPI_SCRIPT)?;
        if out.status != 0 {
            return Err(PlatformError::Io(format!(
                "atspi script failed: {}",
                out.stderr.trim()
            )));
        }
        crate::linux_probe::parse_atspi_json(&out.stdout)
    }

    fn frontmost_pid() -> Result<i32, PlatformError> {
        let runner = SystemRunner;
        let Some(xdotool) = runner.which("xdotool") else {
            return Err(PlatformError::Unsupported {
                platform: "linux".to_string(),
                capability: "desktop.elements".to_string(),
                remediation: "install xdotool for frontmost lookup, or pass --app <pid>"
                    .to_string(),
            });
        };
        let out = runner.run(
            &xdotool.to_string_lossy(),
            &["getactivewindow", "getwindowpid"],
        )?;
        out.stdout.trim().parse::<i32>().map_err(|_| {
            PlatformError::Io(format!(
                "xdotool returned unparsable pid: {}",
                out.stdout.trim()
            ))
        })
    }
}

#[cfg(target_os = "macos")]
fn frontmost_app_pid() -> Result<i32, PlatformError> {
    use objc2::runtime::AnyObject;
    use objc2::{class, msg_send};

    // SAFETY: Querying NSWorkspace sharedWorkspace and frontmostApplication.
    // Handles null pointers safely and extracts the processIdentifier.
    unsafe {
        let workspace: *mut AnyObject = msg_send![class!(NSWorkspace), sharedWorkspace];
        if workspace.is_null() {
            return Err(PlatformError::Io(
                "failed to get shared NSWorkspace".to_string(),
            ));
        }
        let app: *mut AnyObject = msg_send![workspace, frontmostApplication];
        if app.is_null() {
            return Err(PlatformError::Io(
                "no frontmost application found".to_string(),
            ));
        }
        let pid: i32 = msg_send![app, processIdentifier];
        if pid <= 0 {
            return Err(PlatformError::Io(format!(
                "invalid frontmost application process ID: {pid}"
            )));
        }
        Ok(pid)
    }
}

#[cfg(target_os = "macos")]
type AXUIElementRef = *mut std::ffi::c_void;
#[cfg(target_os = "macos")]
type AXValueRef = *mut std::ffi::c_void;
#[cfg(target_os = "macos")]
type CFTypeRef = *mut std::ffi::c_void;
#[cfg(target_os = "macos")]
type CFStringRef = *const std::ffi::c_void;
#[cfg(target_os = "macos")]
type CFArrayRef = *const std::ffi::c_void;

#[cfg(target_os = "macos")]
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct CGPoint {
    x: f64,
    y: f64,
}

#[cfg(target_os = "macos")]
#[repr(C)]
#[derive(Debug, Clone, Copy)]
struct CGSize {
    width: f64,
    height: f64,
}

#[cfg(target_os = "macos")]
const K_AX_VALUE_CGPOINT_TYPE: usize = 1;
#[cfg(target_os = "macos")]
const K_AX_VALUE_CGSIZE_TYPE: usize = 2;
#[cfg(target_os = "macos")]
const K_CF_STRING_ENCODING_UTF8: u32 = 0x0800_0100;

#[cfg(target_os = "macos")]
#[link(name = "ApplicationServices", kind = "framework")]
extern "C" {
    fn AXIsProcessTrusted() -> u8;
    fn AXUIElementCreateApplication(pid: i32) -> AXUIElementRef;
    fn AXUIElementCopyAttributeValue(
        element: AXUIElementRef,
        attribute: CFStringRef,
        value: *mut CFTypeRef,
    ) -> i32;
    fn AXValueGetValue(
        value: AXValueRef,
        the_type: usize,
        value_ptr: *mut std::ffi::c_void,
    ) -> bool;
}

#[cfg(target_os = "macos")]
#[link(name = "CoreFoundation", kind = "framework")]
extern "C" {
    fn CFStringCreateWithCString(
        alloc: *const std::ffi::c_void,
        c_str: *const std::ffi::c_char,
        encoding: u32,
    ) -> CFStringRef;
    fn CFStringGetCString(
        the_string: CFStringRef,
        buffer: *mut std::ffi::c_char,
        buffer_size: isize,
        encoding: u32,
    ) -> bool;
    fn CFStringGetLength(the_string: CFStringRef) -> isize;
    fn CFStringGetMaximumSizeForEncoding(length: isize, encoding: u32) -> isize;
    fn CFGetTypeID(cf: CFTypeRef) -> usize;
    fn CFStringGetTypeID() -> usize;
    fn CFArrayGetTypeID() -> usize;
    fn CFArrayGetCount(the_array: CFArrayRef) -> isize;
    fn CFArrayGetValueAtIndex(the_array: CFArrayRef, idx: isize) -> *const std::ffi::c_void;
    fn CFRelease(cf: CFTypeRef);
}

#[cfg(target_os = "macos")]
struct CfString {
    raw: CFStringRef,
}

#[cfg(target_os = "macos")]
impl CfString {
    fn new(s: &str) -> Option<Self> {
        let c_str = std::ffi::CString::new(s).ok()?;
        // SAFETY: CFStringCreateWithCString creates a new +1 owned CFStringRef.
        let raw = unsafe {
            CFStringCreateWithCString(std::ptr::null(), c_str.as_ptr(), K_CF_STRING_ENCODING_UTF8)
        };
        if raw.is_null() {
            None
        } else {
            Some(Self { raw })
        }
    }

    fn as_cf_str(&self) -> CFStringRef {
        self.raw
    }
}

#[cfg(target_os = "macos")]
impl Drop for CfString {
    fn drop(&mut self) {
        if !self.raw.is_null() {
            // SAFETY: self.raw is +1 owned by CfString and must be released upon drop.
            unsafe {
                CFRelease(self.raw as CFTypeRef);
            }
        }
    }
}

#[cfg(target_os = "macos")]
struct AxAttributes {
    ax_children: CfString,
    ax_windows: CfString,
    ax_role: CfString,
    ax_title: CfString,
    ax_value: CfString,
    ax_position: CfString,
    ax_size: CfString,
}

#[cfg(target_os = "macos")]
impl AxAttributes {
    fn new() -> Option<Self> {
        Some(Self {
            ax_children: CfString::new("AXChildren")?,
            ax_windows: CfString::new("AXWindows")?,
            ax_role: CfString::new("AXRole")?,
            ax_title: CfString::new("AXTitle")?,
            ax_value: CfString::new("AXValue")?,
            ax_position: CfString::new("AXPosition")?,
            ax_size: CfString::new("AXSize")?,
        })
    }
}

#[cfg(target_os = "macos")]
unsafe fn copy_attribute(element: AXUIElementRef, attribute: CFStringRef) -> Option<CFTypeRef> {
    let mut value: CFTypeRef = std::ptr::null_mut();
    // SAFETY: AXUIElementCopyAttributeValue stores a +1 owned CFTypeRef in value on success (0).
    let err = AXUIElementCopyAttributeValue(element, attribute, &mut value);
    if err == 0 && !value.is_null() {
        Some(value)
    } else {
        None
    }
}

#[cfg(target_os = "macos")]
unsafe fn cf_to_string(cf: CFTypeRef) -> Option<String> {
    if cf.is_null() || CFGetTypeID(cf) != CFStringGetTypeID() {
        return None;
    }
    let str_ref = cf as CFStringRef;
    let length = CFStringGetLength(str_ref);
    if length <= 0 {
        return Some(String::new());
    }
    let max_size = CFStringGetMaximumSizeForEncoding(length, K_CF_STRING_ENCODING_UTF8);
    if max_size <= 0 {
        return Some(String::new());
    }
    let mut buf = vec![0u8; (max_size + 1) as usize];
    let ok = CFStringGetCString(
        str_ref,
        buf.as_mut_ptr() as *mut std::ffi::c_char,
        max_size + 1,
        K_CF_STRING_ENCODING_UTF8,
    );
    if ok {
        let c_str = std::ffi::CStr::from_ptr(buf.as_ptr() as *const std::ffi::c_char);
        Some(c_str.to_string_lossy().into_owned())
    } else {
        None
    }
}

#[cfg(target_os = "macos")]
fn traverse_element(
    element: AXUIElementRef,
    pid: i32,
    depth: usize,
    attrs: &AxAttributes,
    elements: &mut Vec<ScreenElement>,
) {
    if elements.len() >= MAX_ELEMENT_CAP {
        return;
    }

    // 1. Read role
    // SAFETY: copy_attribute returns +1 owned CFTypeRef which is released immediately after extraction.
    let role = unsafe {
        if let Some(role_cf) = copy_attribute(element, attrs.ax_role.as_cf_str()) {
            let s = cf_to_string(role_cf).unwrap_or_default();
            CFRelease(role_cf);
            s
        } else {
            String::new()
        }
    };

    // 2. Read label (prefer AXTitle, fallback AXValue string)
    // SAFETY: copy_attribute returns +1 owned CFTypeRefs, each released immediately after extraction.
    let mut label: Option<String> = None;
    unsafe {
        if let Some(title_cf) = copy_attribute(element, attrs.ax_title.as_cf_str()) {
            if let Some(s) = cf_to_string(title_cf) {
                let trimmed = s.trim();
                if !trimmed.is_empty() {
                    label = Some(trimmed.to_string());
                }
            }
            CFRelease(title_cf);
        }
        if label.is_none() {
            if let Some(val_cf) = copy_attribute(element, attrs.ax_value.as_cf_str()) {
                if let Some(s) = cf_to_string(val_cf) {
                    let trimmed = s.trim();
                    if !trimmed.is_empty() {
                        label = Some(trimmed.to_string());
                    }
                }
                CFRelease(val_cf);
            }
        }
    }

    // 3. Read position and size
    let mut x = 0i32;
    let mut y = 0i32;
    let mut w = 0u32;
    let mut h = 0u32;
    unsafe {
        if let Some(pos_cf) = copy_attribute(element, attrs.ax_position.as_cf_str()) {
            let mut pt = CGPoint { x: 0.0, y: 0.0 };
            // SAFETY: AXValueGetValue safely unpacks CGPoint from pos_cf.
            if AXValueGetValue(
                pos_cf as AXValueRef,
                K_AX_VALUE_CGPOINT_TYPE,
                &mut pt as *mut CGPoint as *mut std::ffi::c_void,
            ) {
                x = pt.x as i32;
                y = pt.y as i32;
            }
            CFRelease(pos_cf);
        }

        if let Some(size_cf) = copy_attribute(element, attrs.ax_size.as_cf_str()) {
            let mut sz = CGSize {
                width: 0.0,
                height: 0.0,
            };
            // SAFETY: AXValueGetValue safely unpacks CGSize from size_cf.
            if AXValueGetValue(
                size_cf as AXValueRef,
                K_AX_VALUE_CGSIZE_TYPE,
                &mut sz as *mut CGSize as *mut std::ffi::c_void,
            ) {
                w = sz.width.max(0.0) as u32;
                h = sz.height.max(0.0) as u32;
            }
            CFRelease(size_cf);
        }
    }

    if !role.is_empty() {
        elements.push(ScreenElement {
            role,
            label,
            x,
            y,
            w,
            h,
            pid,
        });
    }

    if depth >= MAX_TRAVERSAL_DEPTH || elements.len() >= MAX_ELEMENT_CAP {
        return;
    }

    // 4. Traverse children
    // SAFETY: copy_attribute returns +1 owned CFTypeRef. Elements within CFArray are borrowed via
    // CFArrayGetValueAtIndex and kept alive during the nested traverse_element call by holding children_ref.
    // children_ref is released when traversal of these children finishes.
    unsafe {
        let children_cf = copy_attribute(element, attrs.ax_children.as_cf_str());
        let mut had_children = false;

        if let Some(children_ref) = children_cf {
            if CFGetTypeID(children_ref) == CFArrayGetTypeID() {
                let count = CFArrayGetCount(children_ref as CFArrayRef);
                if count > 0 {
                    had_children = true;
                    for i in 0..count {
                        if elements.len() >= MAX_ELEMENT_CAP {
                            break;
                        }
                        let child =
                            CFArrayGetValueAtIndex(children_ref as CFArrayRef, i) as AXUIElementRef;
                        if !child.is_null() {
                            traverse_element(child, pid, depth + 1, attrs, elements);
                        }
                    }
                }
            }
            CFRelease(children_ref);
        }

        // Fallback for root element (depth 0): if AXChildren yielded nothing, try AXWindows.
        if !had_children && depth == 0 {
            if let Some(windows_ref) = copy_attribute(element, attrs.ax_windows.as_cf_str()) {
                if CFGetTypeID(windows_ref) == CFArrayGetTypeID() {
                    let count = CFArrayGetCount(windows_ref as CFArrayRef);
                    for i in 0..count {
                        if elements.len() >= MAX_ELEMENT_CAP {
                            break;
                        }
                        let child =
                            CFArrayGetValueAtIndex(windows_ref as CFArrayRef, i) as AXUIElementRef;
                        if !child.is_null() {
                            traverse_element(child, pid, depth + 1, attrs, elements);
                        }
                    }
                }
                CFRelease(windows_ref);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_filter_by_role_synthetic() {
        let elements = vec![
            ScreenElement {
                role: "AXButton".to_string(),
                label: Some("OK".to_string()),
                x: 100,
                y: 200,
                w: 80,
                h: 30,
                pid: 1234,
            },
            ScreenElement {
                role: "AXWindow".to_string(),
                label: Some("Main Window".to_string()),
                x: 0,
                y: 0,
                w: 1024,
                h: 768,
                pid: 1234,
            },
            ScreenElement {
                role: "AXTextField".to_string(),
                label: None,
                x: 100,
                y: 150,
                w: 200,
                h: 24,
                pid: 1234,
            },
            ScreenElement {
                role: "AXButton".to_string(),
                label: Some("Cancel".to_string()),
                x: 190,
                y: 200,
                w: 80,
                h: 30,
                pid: 1234,
            },
        ];

        let buttons = filter_by_role(&elements, "AXButton");
        assert_eq!(buttons.len(), 2);
        assert_eq!(buttons[0].label.as_deref(), Some("OK"));
        assert_eq!(buttons[1].label.as_deref(), Some("Cancel"));

        let buttons_case_insensitive = filter_by_role(&elements, "axbutton");
        assert_eq!(buttons_case_insensitive.len(), 2);

        let buttons_stripped_prefix = filter_by_role(&elements, "Button");
        assert_eq!(buttons_stripped_prefix.len(), 2);

        let windows = filter_by_role(&elements, "AXWindow");
        assert_eq!(windows.len(), 1);
        assert_eq!(windows[0].label.as_deref(), Some("Main Window"));

        let none = filter_by_role(&elements, "AXSlider");
        assert!(none.is_empty());
    }

    #[test]
    fn test_read_elements_live_or_permission_gate() {
        let trusted = accessibility_trusted();
        let result = read_elements(AppTarget::Frontmost);

        if trusted {
            let elements =
                result.expect("read_elements must succeed when accessibility is trusted");
            assert!(
                !elements.is_empty(),
                "expected at least one element for frontmost target"
            );
            for elem in &elements {
                assert!(!elem.role.is_empty(), "element role must not be empty");
                assert_eq!(elem.pid, elements[0].pid);
            }

            // Also verify resolving by PID directly
            let target_pid = elements[0].pid;
            let pid_elements = read_elements(AppTarget::Pid(target_pid))
                .expect("read_elements by pid must succeed");
            assert!(
                !pid_elements.is_empty(),
                "expected at least one element for target pid {target_pid}"
            );

            // Also verify self PID returns Ok
            let self_elements = read_elements(AppTarget::Pid(std::process::id() as i32))
                .expect("read_elements for self process must succeed");
            for elem in &self_elements {
                assert!(!elem.role.is_empty());
                assert_eq!(elem.pid, std::process::id() as i32);
            }
        } else {
            let err = result
                .expect_err("read_elements must return error when accessibility is untrusted");
            match err {
                PlatformError::PermissionRequired { scope, deep_link } => {
                    assert_eq!(scope, "accessibility");
                    assert!(deep_link.contains("Privacy_Accessibility"));
                }
                other => panic!("expected PermissionRequired, got {other:?}"),
            }
        }
    }
}
