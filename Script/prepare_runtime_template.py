"""把 runtime-only 构建的同一组 EXE/DLL 和许可资源准备为编辑器的发布模板。"""
import argparse
import json
import hashlib
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory

from delivery_resources import REQUIRED


def main():
    """验证宿主模式，从真实运行构建部署资源，最后一次性发布新模板目录。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True, type=Path, help="通过 --runtime-only 生成的 CatalpaBox.exe")
    parser.add_argument("--output", required=True, type=Path, help="开发宿主旁尚不存在的 RuntimeTemplate 目录")
    parser.add_argument("--config-root", type=Path, default=Path(__file__).resolve().parents[1] / "Config")
    parser.add_argument("--notices", type=Path, default=Path(__file__).resolve().parents[1] / "ThirdPartyNotices.md")
    args = parser.parse_args()
    host = args.host.resolve(strict=True)
    check = subprocess.run([str(host), "--help"], capture_output=True)
    if check.returncode or b"Usage: CatalpaBox --run-package <absolute-file> | --help" not in check.stdout + check.stderr:
        parser.error("需要由 --runtime-only 构建的 CatalpaBox，不能部署编辑器宿主")
    output = args.output.resolve()
    if output.exists():
        parser.error("模板目标目录已存在，请选择新的目录")
    output.parent.mkdir(parents=True, exist_ok=True)
    files = {"CatalpaBox.exe": host, "Catalpa.dll": host.parent / "Catalpa.dll",
             "ThirdPartyNotices.md": args.notices.resolve(strict=True)}
    for name in REQUIRED:
        if not name.startswith("Shaders/"):
            asset = host.parent / "Resources" / name
            if name in ("Config/InputProfiles.json", "Config/RendererProfiles.json") and not asset.is_file():
                asset = args.config_root / Path(name).name
            files["Resources/" + name] = asset
    # 同一运行构建中的动态库与运行配置随宿主一并交付。着色器由最终 Cook 时的清单选定。
    for library in host.parent.glob("*.dll"):
        files.setdefault(library.name, library)
    for config in (host.parent / "Resources/Config").glob("*.json"):
        files["Resources/Config/" + config.name] = config
    source = Path(__file__).resolve().parents[1] / "Catalpa/third_party"
    # 独立运行目录保留库和字体的原始许可，便于发行时核对版权和通知。
    for relative in (
            "glm/copying.txt", "entt/LICENSE", "enkiTS/License.txt", "glfw/LICENSE.md",
            "imgui/LICENSE.txt", "spdlog/LICENSE", "VulkanMemoryAllocator/LICENSE.txt", "json/LICENSE.MIT",
            "JoltPhysics/LICENSE", "miniaudio/LICENSE", "RmlUi/LICENSE.txt", "recastnavigation/License.txt",
            "freetype/LICENSE.TXT", "freetype/docs/FTL.TXT", "lua/doc/readme.html", "zstd/LICENSE",
            "stb/LICENSE", "SPIRV-Reflect/LICENSE"):
        files["Licenses/" + relative] = source / relative
    with TemporaryDirectory(prefix=".catalpa-template-", dir=output.parent) as temporary:
        staged = Path(temporary) / "template"
        staged.mkdir()
        for name, source in files.items():
            target = staged / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source.resolve(strict=True), target)
        hashes = {}
        for name in files:
            with (staged / name).open("rb") as stream:
                hashes[name] = hashlib.file_digest(stream, "sha256").hexdigest()
        description = {"schemaVersion": 1, "runtimeOnly": True, "files": sorted(files), "sha256": hashes}
        (staged / "runtime-template.json").write_text(json.dumps(description, indent=2), encoding="utf-8")
        staged.rename(output)
    print(output)


if __name__ == "__main__":
    main()
