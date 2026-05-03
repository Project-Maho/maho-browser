fn main() {
    let n = std::env::args()
        .nth(1)
        .and_then(|s| s.parse().ok())
        .unwrap_or(1000);
    let results = maho_bench::run_comparison(n);
    maho_bench::print_comparison(&results);
}
