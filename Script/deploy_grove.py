"""Cook the bundled courtyard with the editor host and publish its author scene last."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def publish(path, data):
    if path.is_file() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        temporary.write_bytes(data)
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, type=Path)
    parser.add_argument("--work", required=True, type=Path)
    parser.add_argument("--configuration", choices=("Debug", "Release", "Dist"), required=True)
    args = parser.parse_args()
    host = args.host.resolve(strict=True)
    project = Path(__file__).resolve().parents[1] / "Projects" / "Grove"
    content = project / "Content"
    resources = host.parent / "Resources"
    destination = resources / "Grove"
    work = args.work.resolve()

    # 只写构建配置的派生树，作者文件由现有导入器按只读 source root 处理。
    config = json.loads((project / "Grove.catalpaproj").read_text(encoding="utf-8"))
    config.update(contentRoot=str(content), buildProfiles=str(project / "BuildProfiles.json"),
                  buildConfiguration=args.configuration, cookedRoot=str(destination / "Cooked"),
                  derivedDataRoot=str(work / "DerivedDataCache"))
    staged = work / "Grove.catalpaproj"
    publish(staged, json.dumps(config, indent=2).encode("utf-8"))
    for name in ("Grove.glb", "Courtyard.hdr", "Grove.scene.json"):
        subprocess.run([str(host), "--cook-assets", "--project", str(staged),
                        "--source", str(content / name), "--shader-root", str(resources / "Shaders")], check=True)
    publish(destination / "Grove.scene.json", (content / "Grove.scene.json").read_bytes())


if __name__ == "__main__":
    main()
