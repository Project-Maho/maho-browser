"""Keep desktop build templates capable of H.264/AAC media playback."""

from pathlib import Path
import re
import unittest


class TestMediaCodecConfig(unittest.TestCase):
    def test_desktop_template_enables_h264_and_aac(self) -> None:
        platforms = ["mac", "linux", "win"]
        variants = ["", "_debug", "_release"]
        for platform in platforms:
            for variant in variants:
                with self.subTest(platform=platform, variant=variant):
                    template = (
                        Path(__file__).resolve().parent.parent
                        / "config"
                        / f"args_{platform}{variant}.gn"
                    )

                    arguments = dict(
                        re.findall(
                            r'^\s*(proprietary_codecs|ffmpeg_branding|enable_widevine)\s*=\s*'
                            r'(true|false|"[^"]*")\s*(?:#.*)?$',
                            template.read_text(encoding="utf-8"),
                            re.MULTILINE,
                        )
                    )

                    self.assertEqual(
                        arguments,
                        {
                            "proprietary_codecs": "true",
                            "ffmpeg_branding": '"Chrome"',
                            "enable_widevine": "true",
                        },
                        f"Failed for args_{platform}{variant}.gn",
                    )


if __name__ == "__main__":
    unittest.main()
