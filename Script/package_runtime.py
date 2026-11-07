"""Deploy a manifest-selected host and a validated package into a new isolated directory."""
import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import hashlib
from delivery_resources import REQUIRED, validate_resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--tool-host", type=Path, help="Development host providing --inspect; runtime-only host stays tool-free")
    args = parser.parse_args()
    host = args.host.resolve(strict=True)
    tool_host = (args.tool_host or args.host).resolve(strict=True)
    manifest = args.manifest.resolve(strict=True)
    output = args.output.resolve()
    if output.exists():
        parser.error("Output must be a new directory; an existing runtime package is never overwritten")
    if manifest.stat().st_size > 1024 * 1024:
        parser.error("Deployment manifest exceeds 1 MiB")
    specification = json.loads(manifest.read_text(encoding="utf-8"))
    if set(specification) != {"schemaVersion", "package", "files"} or specification["schemaVersion"] != 1:
        parser.error("Unsupported deployment manifest")
    if not isinstance(specification['package'], str) or not isinstance(specification['files'], list) or len(specification['files']) > 16384:
        parser.error("Invalid package path or bounded deployment file list")
    package = (manifest.parent / specification["package"]).resolve(strict=True)
    if package.suffix != ".catpkg" or not package.is_relative_to(manifest.parent):
        parser.error("Package must be a .catpkg under the manifest directory")
    # Only the explicitly selected tool host performs bounded format/hash checks; no window is created.
    checked = subprocess.run([str(tool_host), "--inspect", str(package)], check=False)
    if checked.returncode:
        return checked.returncode
    if not all(re.fullmatch(r"[A-Za-z0-9_.-]+", file.name) for file in (host, package)):
        parser.error("Host/package file names must use ASCII letters, digits, dots, underscores or hyphens")
    planned = [(host, host.name), (package, package.name)]
    for item in specification["files"]:
        if not isinstance(item, str) or "\\" in item or ":" in item:
            parser.error("Deployment files must be relative portable paths")
        relative = Path(item)
        if relative.is_absolute() or ".." in relative.parts or not relative.parts:
            parser.error("Deployment path escapes its root")
        source = (manifest.parent / relative).resolve(strict=True)
        if not source.is_relative_to(manifest.parent) or not source.is_file():
            parser.error("Deployment source escapes its root or is not a file")
        planned.append((source, relative.as_posix()))
    if not any(name.casefold() == 'catalpa.dll' for _, name in planned):
        parser.error('Deployment must explicitly include Catalpa.dll')
    selected = {name.casefold() for _, name in planned}
    if any(('Resources/' + name).casefold() not in selected for name in REQUIRED):
        parser.error('Deployment omits required runtime Config, cooked shaders or the configured font')
    names = [name.casefold() for _, name in planned]
    occupied = set(names) | {'run.cmd', 'deployment.json', 'runtime-manifest.json'}
    if any(parent.as_posix().casefold() in occupied
           for name in occupied for parent in Path(name).parents if parent != Path('.')):
        parser.error('A deployment file collides with another file directory prefix')
    if any(name in ("run.cmd", "deployment.json", "runtime-manifest.json") for name in names) or len(set(names)) != len(names):
        parser.error("Deployment paths collide")
    # Validate all paths before writes; inspect the copied closure before publishing its launch/manifest files.
    output.mkdir(parents=True, exist_ok=False)
    for source, relative in planned:
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
    # 发布证据对应实际副本；构建进程可能在首次检查之后替换源包。
    checked = subprocess.run([str(tool_host), '--inspect', str(output / package.name)], check=False)
    if checked.returncode:
        return checked.returncode
    validate_resources(output / 'Resources', tool_host)
    (output / "run.cmd").write_text(f'@"%~dp0{host.name}" --run-package "%~dp0{package.name}"\n', encoding="utf-8")
    shutil.copy2(manifest, output / "deployment.json")
    hashes = {}
    for _, name in planned:
        with (output / name).open('rb') as deployed:
            digest = hashlib.sha256()
            while block := deployed.read(1024 * 1024):
                digest.update(block)
            hashes[name] = digest.hexdigest()
    (output / 'runtime-manifest.json').write_text(json.dumps({'schemaVersion': 1, 'files': hashes,
        'validation': 'package inspected; runtime execution not performed by deployment'}, indent=2) + '\n', encoding='utf-8')
    print(f"Runtime package: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
