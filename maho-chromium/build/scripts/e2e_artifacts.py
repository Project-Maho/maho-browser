#!/usr/bin/env python3
"""Artifact helpers for Chromium Maho E2E runs."""

import os
import time
from dataclasses import dataclass

import Foundation as _FD
import Quartz as _Q

NSURL = getattr(_FD, 'NSURL')
CGWindowListCreateImage = getattr(_Q, 'CGWindowListCreateImage')
CGImageDestinationCreateWithURL = getattr(_Q, 'CGImageDestinationCreateWithURL')
CGImageDestinationAddImage = getattr(_Q, 'CGImageDestinationAddImage')
CGImageDestinationFinalize = getattr(_Q, 'CGImageDestinationFinalize')
CGRectInfinite = getattr(_Q, 'CGRectInfinite')
kUTTypePNG = 'public.png'


def _timestamp() -> str:
    return time.strftime('%Y%m%d-%H%M%S')


@dataclass(frozen=True)
class ArtifactSet:
    root: str
    scenario_name: str

    @property
    def scenario_dir(self) -> str:
        return os.path.join(self.root, self.scenario_name)

    def path(self, name: str) -> str:
        return os.path.join(self.scenario_dir, name)

    def ensure(self) -> None:
        os.makedirs(self.scenario_dir, exist_ok=True)

    def write_text(self, name: str, text: str) -> str:
        path = self.path(name)
        with open(path, 'w', encoding='utf-8') as handle:
            handle.write(text)
        return path


def resolve_artifact_root(override_root: str | None) -> str:
    if override_root:
        return override_root
    return os.path.join('/tmp', 'maho-chromium-e2e-spaces')


def create_artifacts(root: str, scenario_name: str) -> ArtifactSet:
    artifact_set = ArtifactSet(root=root, scenario_name=scenario_name)
    artifact_set.ensure()
    return artifact_set


def default_profile_dir() -> str:
    return os.path.join('/tmp', f'maho-e2e-profile-{_timestamp()}')


def write_profile_path(artifacts: ArtifactSet, profile_dir: str) -> str:
    return artifacts.write_text('profile_path.txt', profile_dir)


def capture_screenshot_to_path(rect, output_path: str) -> bool:
    image = CGWindowListCreateImage(rect, 1, 0, 0)
    if not image:
        return False
    url = NSURL.fileURLWithPath_(output_path)
    destination = CGImageDestinationCreateWithURL(url, kUTTypePNG, 1, None)
    if not destination:
        return False
    CGImageDestinationAddImage(destination, image, None)
    return bool(CGImageDestinationFinalize(destination))


def capture_fullscreen_png(output_path: str) -> bool:
    return capture_screenshot_to_path(CGRectInfinite, output_path)
