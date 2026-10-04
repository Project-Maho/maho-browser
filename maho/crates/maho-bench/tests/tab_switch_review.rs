// Exercise the same setup and measured loop as the Criterion target.
include!("../benches/tab_switch.rs");

mod tests {
    use super::*;

    fn fixture() -> (MahoCore, TabId, TabId) {
        let mut core = MahoCore::new();
        let space = core.get_active_space_id();
        let source = extract_tab_id_from_updates(&mut core, &space, "https://source.test");
        let target = extract_tab_id_from_updates(&mut core, &space, "https://target.test");
        activate_tab(&mut core, &source);
        (core, source, target)
    }

    #[test]
    fn every_active_sample_switches_from_the_source() {
        let (mut core, source, target) = fixture();
        measure_switches(&mut core, &source, &target, false, 2);
    }

    #[test]
    fn every_resume_sample_starts_suspended() {
        let (mut core, source, target) = fixture();
        measure_switches(&mut core, &source, &target, true, 2);
    }
}
