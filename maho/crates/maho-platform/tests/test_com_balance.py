"""Run the actual Windows read_elements control flow with a COM ownership double.

No Windows SDK/runtime is needed: the boundary double models successful COM
initialization (including S_FALSE), changed apartment mode, and a later UIA
failure. It deliberately does not emulate UI Automation tree traversal.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

PRELUDE = r'''
use std::cell::Cell;
#[derive(Debug)]
enum PlatformError { Io(String) }
enum AppTarget { Pid(i32), Frontmost }
#[derive(Debug)]
struct ScreenElement;
#[derive(Clone, Copy, PartialEq)]
struct HResult(i32);
impl HResult { fn is_err(self) -> bool { self.0 < 0 } fn is_ok(self) -> bool { self.0 >= 0 } }
const RPC_E_CHANGED_MODE: HResult = HResult(-2147417850);
const COINIT_MULTITHREADED: u32 = 0;
const CLSCTX_INPROC_SERVER: u32 = 1;
const AUTOMATION: u32 = 0;
use AUTOMATION as CUIAutomation8;
thread_local! {
    static OWNERS: Cell<i32> = const { Cell::new(0) };
    static INIT: Cell<i32> = const { Cell::new(0) };
}
unsafe fn initialize(_: Option<()>, _: u32) -> HResult {
    let status = INIT.get();
    if status >= 0 { OWNERS.set(OWNERS.get() + 1); }
    HResult(status)
}
unsafe fn uninitialize() { OWNERS.set(OWNERS.get() - 1); }
use initialize as CoInitializeEx;
use uninitialize as CoUninitialize;
struct Automation;
use Automation as IUIAutomation;
struct Walker;
struct Element;
impl Automation {
    fn ControlViewWalker(&self) -> Result<Walker, PlatformError> { Ok(Walker) }
    fn GetRootElement(&self) -> Result<Element, PlatformError> { Ok(Element) }
}
impl Walker {
    fn GetFirstChildElement(&self, _: &Element) -> Result<Element, PlatformError> { Err(PlatformError::Io("no children".into())) }
    fn GetNextSiblingElement(&self, _: &Element) -> Result<Element, PlatformError> { Err(PlatformError::Io("no siblings".into())) }
}
impl Element { fn CurrentProcessId(&self) -> Result<i32, PlatformError> { Ok(42) } }
unsafe fn create(_: &u32, _: Option<()>, _: u32) -> Result<Automation, PlatformError> {
    Err(PlatformError::Io("UI Automation unavailable".into()))
}
use create as CoCreateInstance;
fn frontmost_pid() -> Result<i32, PlatformError> { Ok(42) }
fn com_err(error: PlatformError) -> PlatformError { error }
unsafe fn walk(_: &Walker, _: &Element, _: i32, _: usize, _: &mut Vec<ScreenElement>) {}
'''

TESTS = r'''
#[test]
fn com_initialization_is_balanced_when_uia_fails() {
    for (status, initial_owners) in [(0, 0), (1, 1), (-2147417850, 1)] {
        INIT.set(status);
        OWNERS.set(initial_owners);
        let result = read_elements(AppTarget::Pid(42));
        assert!(matches!(result, Err(PlatformError::Io(ref reason)) if reason.contains("UI Automation")));
        assert_eq!(OWNERS.get(), initial_owners, "COM ownership leaked for HRESULT {status}");
    }
    let _ = AppTarget::Frontmost;
    let _cleanup: unsafe fn() = CoUninitialize;
}
'''


class ComBalanceTest(unittest.TestCase):
    def test_actual_read_elements_balances_com(self):
        source = (ROOT / "src/win_ax.rs").read_text()
        start = source.index("pub fn read_elements(")
        end = source.index("\nunsafe fn walk(", start)
        with tempfile.TemporaryDirectory(dir=ROOT / "tests") as directory:
            path = Path(directory)
            rust = path / "com_balance.rs"
            rust.write_text(PRELUDE + source[start:end] + TESTS)
            binary = path / "com_balance"
            compiled = subprocess.run(["rustc", "--edition=2021", "--test", str(rust), "-o", str(binary)], text=True, capture_output=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            result = subprocess.run([str(binary)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
