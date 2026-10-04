use maho_import::parsers::chromium::favicons::parse_chromium_favicons;
use std::path::PathBuf;
fn main() {
    let dir: PathBuf = std::env::args().nth(1).unwrap().into();
    match parse_chromium_favicons(&dir) {
        Ok(v) => {
            println!(
                "entries={} total_page_urls={}",
                v.len(),
                v.iter().map(|e| e.page_urls.len()).sum::<usize>()
            );
        }
        Err(e) => eprintln!("err: {}", e),
    }
}
