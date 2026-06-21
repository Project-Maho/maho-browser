/// Wraps an FFI function body in `catch_unwind` to prevent panics from unwinding
/// across the C ABI boundary (which is undefined behavior).
///
/// # Usage
/// ```ignore
/// ffi_safe!({
///     // function body here
///     some_computation()
/// }, fallback_value)
/// ```
///
/// The fallback is returned when the body panics. Choose the fallback based on
/// the function's return type:
/// - `*mut T` → `std::ptr::null_mut()`
/// - `bool` → `false`
/// - `i32` / `i64` / `u64` / `usize` / `i8` → `0`
/// - `f64` → `0.0`
/// - `()` → `()`
#[macro_export]
macro_rules! ffi_safe {
    ($body:expr, $fallback:expr) => {
        match ::std::panic::catch_unwind(::std::panic::AssertUnwindSafe(|| $body)) {
            Ok(v) => v,
            Err(_) => $fallback,
        }
    };
}
