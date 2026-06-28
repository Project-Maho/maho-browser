use crate::updates::releases::ReleaseManifest;

pub fn build_appcast_xml(channel: &str, base_url: &str, item: Option<&ReleaseManifest>) -> String {
    let mut out = String::new();
    out.push_str(r#"<?xml version="1.0" standalone="yes"?>"#);
    out.push('\n');
    out.push_str(
        r#"<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle" version="2.0">"#,
    );
    out.push('\n');
    out.push_str("  <channel>\n");
    out.push_str(&format!(
        "    <title>{}</title>\n",
        escape_xml(&format!("Maho Browser {channel}"))
    ));
    out.push_str(&format!(
        "    <link>{}</link>\n",
        escape_xml(&format!("{base_url}/updates/macos/appcast-{channel}.xml"))
    ));

    if let Some(m) = item {
        out.push_str("    <item>\n");
        out.push_str(&format!(
            "      <title>{}</title>\n",
            escape_xml(&format!("Version {}", m.version))
        ));
        out.push_str(&format!(
            "      <pubDate>{}</pubDate>\n",
            escape_xml(&m.release_date)
        ));
        out.push_str(&format!(
            "      <sparkle:version>{}</sparkle:version>\n",
            escape_xml(&m.version)
        ));
        out.push_str(&format!(
            "      <sparkle:shortVersionString>{}</sparkle:shortVersionString>\n",
            escape_xml(&m.version)
        ));
        if let Some(notes) = &m.release_notes_url {
            out.push_str(&format!(
                "      <sparkle:releaseNotesLink>{}</sparkle:releaseNotesLink>\n",
                escape_xml(notes)
            ));
        }
        let url = format!(
            "{base_url}/updates/macos/{channel}/{}",
            m.binary_filename
        );
        let sig_attr = m
            .binary_ed25519_signature_base64
            .as_deref()
            .map(|s| format!(r#" sparkle:edSignature="{}""#, escape_xml(s)))
            .unwrap_or_default();
        out.push_str(&format!(
            "      <enclosure url=\"{}\"{} length=\"{}\" type=\"application/octet-stream\" />\n",
            escape_xml(&url),
            sig_attr,
            m.binary_size
        ));
        out.push_str("    </item>\n");
    }

    out.push_str("  </channel>\n");
    out.push_str("</rss>\n");
    out
}

fn escape_xml(s: &str) -> String {
    s.replace('&', "&amp;")
        .replace('<', "&lt;")
        .replace('>', "&gt;")
        .replace('"', "&quot;")
        .replace('\'', "&apos;")
}

#[cfg(test)]
mod tests {
    use super::*;

    fn fixture_manifest() -> ReleaseManifest {
        ReleaseManifest {
            version: "149.0.7812.0".into(),
            channel: "stable".into(),
            binary_filename: "Maho-149.0.7812.0.dmg".into(),
            binary_sha256: "a".repeat(64),
            binary_size: 123_456_789,
            installer_signer_cn: "Maho Browser Inc.".into(),
            rollout_bucket: 100,
            release_date: "Mon, 15 Jun 2026 00:00:00 +0000".into(),
            release_notes_url: Some("https://maho.dev/release-notes/149".into()),
            binary_ed25519_signature_base64: Some("c2lnbmF0dXJl".into()),
        }
    }

    #[test]
    fn empty_appcast_when_no_release() {
        let xml = build_appcast_xml("stable", "https://update.maho.dev", None);
        assert!(xml.contains("<title>Maho Browser stable</title>"));
        assert!(!xml.contains("<item>"));
    }

    #[test]
    fn populated_appcast_includes_signature_and_url() {
        let m = fixture_manifest();
        let xml = build_appcast_xml("stable", "https://update.maho.dev", Some(&m));
        assert!(xml.contains("<sparkle:version>149.0.7812.0</sparkle:version>"));
        assert!(xml.contains("sparkle:edSignature=\"c2lnbmF0dXJl\""));
        assert!(xml.contains(
            "url=\"https://update.maho.dev/updates/macos/stable/Maho-149.0.7812.0.dmg\""
        ));
        assert!(xml.contains("length=\"123456789\""));
    }

    #[test]
    fn xml_escapes_dangerous_characters() {
        let mut m = fixture_manifest();
        m.version = "1.0 <script>".into();
        let xml = build_appcast_xml("stable", "https://x", Some(&m));
        assert!(xml.contains("&lt;script&gt;"));
        assert!(!xml.contains("<script>"));
    }
}
