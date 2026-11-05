"""使用指定的工具宿主，将源素材构建到项目配置的派生输出目录。"""
import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--project", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--shader-root", type=Path, required=True)
    parser.add_argument("--unit-scale", type=float, default=1.0)
    parser.add_argument("--source-up", choices=("auto", "y", "z"), default="auto")
    parser.add_argument("--no-generate-normals", action="store_true")
    parser.add_argument("--no-generate-tangents", action="store_true")
    parser.add_argument("--remap", action="append", default=[], metavar="URI=PATH")
    args = parser.parse_args()
    paths = (args.host, args.project, args.source, args.shader_root)
    if any(not path.is_absolute() for path in paths):
        parser.error("host、project、source 和 shader-root 必须使用绝对路径")
    command = [
        str(args.host), "--cook-assets",
        "--project", str(args.project),
        "--source", str(args.source),
        "--shader-root", str(args.shader_root),
        "--unit-scale", str(args.unit_scale),
        "--source-up", args.source_up,
    ]
    if args.no_generate_normals:
        command.append("--no-generate-normals")
    if args.no_generate_tangents:
        command.append("--no-generate-tangents")
    for remap in args.remap:
        command.extend(("--remap", remap))
    try:
        return subprocess.run(command, check=False).returncode
    except OSError as error:
        print(str(error), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
