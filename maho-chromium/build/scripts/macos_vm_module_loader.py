from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
from types import ModuleType


def load_sibling(caller: str, module_name: str) -> ModuleType:
    path = Path(caller).with_name(f"{module_name}.py")
    spec = importlib.util.spec_from_file_location(module_name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"unable to load {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module
