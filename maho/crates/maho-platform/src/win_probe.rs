//! Pure helpers for the Windows grounding backends. Platform-independent so
//! they unit-test on every host; the cfg(windows) backend modules glue them to
//! the OS APIs.

/// Convert in-place a top-down 32bpp BGRA buffer (GDI GetDIBits layout) to RGBA.
pub fn bgra_to_rgba_inplace(buf: &mut [u8]) {
    for px in buf.chunks_exact_mut(4) {
        px.swap(0, 2);
        // BI_RGB's fourth byte is reserved, not an alpha channel.
        px[3] = 255;
    }
}

#[cfg(test)]
mod tests {
    use super::bgra_to_rgba_inplace;

    #[test]
    fn bgra_swap_produces_rgba() {
        let mut buf = vec![1, 2, 3, 255, 4, 5, 6, 255];
        bgra_to_rgba_inplace(&mut buf);
        assert_eq!(buf, vec![3, 2, 1, 255, 6, 5, 4, 255]);
    }

    #[test]
    fn gdi_reserved_byte_does_not_make_screenshot_transparent() {
        let mut buf = [1, 2, 3, 0, 4, 5, 6, 127];
        bgra_to_rgba_inplace(&mut buf);
        assert_eq!(buf, [3, 2, 1, 255, 6, 5, 4, 255]);
    }

    #[test]
    fn bgra_handles_empty_buffer() {
        let mut buf: Vec<u8> = Vec::new();
        bgra_to_rgba_inplace(&mut buf);
        assert!(buf.is_empty());
    }
}
