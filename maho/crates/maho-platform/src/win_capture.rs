//! Windows screen capture via GDI BitBlt of the full virtual screen, encoded
//! as PNG. Failures surface as typed Io/Unsupported errors with the cause.

#![cfg(target_os = "windows")]

use crate::error::PlatformError;
use crate::grounding::CaptureInfo;
use std::path::PathBuf;

use windows_sys::Win32::Graphics::Gdi::{
    BitBlt, CreateCompatibleBitmap, CreateCompatibleDC, DeleteDC, DeleteObject, GetDC, GetDIBits,
    ReleaseDC, SelectObject, BITMAPINFO, BITMAPINFOHEADER, BI_RGB, DIB_RGB_COLORS, SRCCOPY,
};
use windows_sys::Win32::UI::WindowsAndMessaging::{
    GetSystemMetrics, SM_CXVIRTUALSCREEN, SM_CYVIRTUALSCREEN, SM_XVIRTUALSCREEN, SM_YVIRTUALSCREEN,
};

fn encode_png(
    path: &std::path::Path,
    rgba: &[u8],
    width: u32,
    height: u32,
) -> Result<(), PlatformError> {
    let file = std::fs::File::create(path).map_err(|e| PlatformError::Io(e.to_string()))?;
    let mut encoder = png::Encoder::new(file, width, height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let mut writer = encoder
        .write_header()
        .map_err(|e| PlatformError::Io(e.to_string()))?;
    writer
        .write_image_data(rgba)
        .map_err(|e| PlatformError::Io(e.to_string()))
}

pub fn capture_screen(output: Option<PathBuf>) -> Result<CaptureInfo, PlatformError> {
    unsafe { capture_screen_inner(output) }
}

unsafe fn capture_screen_inner(output: Option<PathBuf>) -> Result<CaptureInfo, PlatformError> {
    let x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    let y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    let w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    let h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if w <= 0 || h <= 0 {
        return Err(PlatformError::Io("no virtual screen metrics".into()));
    }
    let (w, h) = (w as u32, h as u32);

    let hdc = GetDC(std::ptr::null_mut());
    if hdc.is_null() {
        return Err(PlatformError::Io("GetDC(screen) failed".into()));
    }
    let mem = CreateCompatibleDC(hdc);
    let bmp = CreateCompatibleBitmap(hdc, w as i32, h as i32);
    let old = SelectObject(mem, bmp);

    let mut info: BITMAPINFO = std::mem::zeroed();
    info.bmiHeader.biSize = std::mem::size_of::<BITMAPINFOHEADER>() as u32;
    info.bmiHeader.biWidth = w as i32;
    info.bmiHeader.biHeight = -(h as i32);
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    let mut pixels = vec![0u8; (w as usize) * (h as usize) * 4];
    let mut ok = false;
    if !mem.is_null() && !bmp.is_null() {
        if BitBlt(mem, 0, 0, w as i32, h as i32, hdc, x, y, SRCCOPY) != 0 {
            ok = GetDIBits(
                mem,
                bmp,
                0,
                h,
                pixels.as_mut_ptr().cast(),
                &mut info,
                DIB_RGB_COLORS,
            ) != 0;
        }
    }

    SelectObject(mem, old);
    if !bmp.is_null() {
        DeleteObject(bmp);
    }
    if !mem.is_null() {
        DeleteDC(mem);
    }
    ReleaseDC(std::ptr::null_mut(), hdc);

    if !ok {
        return Err(PlatformError::Io("BitBlt/GetDIBits failed".into()));
    }

    crate::win_probe::bgra_to_rgba_inplace(&mut pixels);
    let target_path = crate::capture::capture_target_path(output)?;
    encode_png(&target_path, &pixels, w, h)?;
    Ok(CaptureInfo {
        path: target_path.to_string_lossy().into_owned(),
        width: w,
        height: h,
    })
}
