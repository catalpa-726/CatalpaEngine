"""Shared bounded resource checks; the selected tool host validates the actual cooked shader closure."""
import json
from pathlib import Path
import subprocess


REQUIRED = (
    'Config/InputProfiles.json', 'Config/RendererProfiles.json', 'Config/AudioDefaults.json',
    'Config/PhysicsDefaults.json', 'Config/UIRuntimeDefaults.json',
    'Shaders/BuiltinShaders.manifest', 'Fonts/Inter/InterVariable.ttf',
)


def validate_resources(directory: Path, tool_host: Path | None = None):
    directory = directory.resolve(strict=True)
    for name in REQUIRED:
        file = (directory / name).resolve(strict=True)
        if not file.is_relative_to(directory) or not file.is_file():
            raise ValueError(f'Runtime resource missing or escapes its root: {name}')
        if file.suffix == '.json':
            if file.stat().st_size > 65536:
                raise ValueError(f'Runtime configuration exceeds 64 KiB: {name}')
            if not isinstance(json.loads(file.read_text(encoding='utf-8')), dict):
                raise ValueError(f'Runtime configuration is not an object: {name}')
    if tool_host:
        result = subprocess.run([str(tool_host), '--inspect',
                                 str(directory / 'Shaders/BuiltinShaders.manifest')], check=False)
        if result.returncode:
            raise ValueError(f'Cooked shader closure inspection failed with exit code {result.returncode}')
