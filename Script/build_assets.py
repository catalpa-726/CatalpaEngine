"""使用指定的工具宿主，将源素材构建到项目配置的派生输出目录。"""
import argparse
from pathlib import Path
import subprocess
import sys
import json
import math


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", type=Path, required=True)
    parser.add_argument("--project", type=Path)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--shader-root", type=Path)
    parser.add_argument("--batch", type=Path, help="Bounded JSON build list; paths resolve relative to this file")
    parser.add_argument("--unit-scale", type=float, default=1.0)
    parser.add_argument("--source-up", choices=("auto", "y", "z"), default="auto")
    parser.add_argument("--no-generate-normals", action="store_true")
    parser.add_argument("--no-generate-tangents", action="store_true")
    parser.add_argument("--remap", action="append", default=[], metavar="URI=PATH")
    args = parser.parse_args()
    if not math.isfinite(args.unit_scale) or args.unit_scale <= 0:
        parser.error('unit-scale must be finite and positive')
    if args.batch:
        if any((args.source, args.project)):
            parser.error('Use either batch or individual source/project options')
        batch = args.batch.resolve(strict=True)
        if batch.stat().st_size > 1024 * 1024:
            parser.error('Batch input exceeds 1 MiB')
        specification = json.loads(batch.read_text(encoding='utf-8'))
        if set(specification) != {'schemaVersion', 'project', 'shaderRoot', 'sources'} or specification['schemaVersion'] != 1:
            parser.error('Unsupported asset-build schema')
        sources = specification['sources']
        if not isinstance(sources, list) or not 0 < len(sources) <= 4096 or not all(isinstance(s, str) and s for s in sources):
            parser.error('Expected 1..4096 explicit source paths')
        if not args.host.is_absolute():
            parser.error('host must be absolute')
        project = (batch.parent / specification['project']).resolve(strict=True)
        if args.shader_root and not args.shader_root.is_absolute():
            parser.error('Batch shader-root override must be absolute')
        shaders = (args.shader_root or (batch.parent / specification['shaderRoot'])).resolve(strict=True)
        planned = [(batch.parent / name).resolve(strict=True) for name in sources]
        if len(set(planned)) != len(planned) or not all(p.is_file() for p in planned):
            parser.error('Duplicate or invalid build source')
        for source in planned:
            command = [sys.executable, str(Path(__file__).resolve()), '--host', str(args.host),
                       '--project', str(project), '--shader-root', str(shaders), '--source', str(source),
                       '--unit-scale', str(args.unit_scale), '--source-up', args.source_up]
            if args.no_generate_normals:
                command.append('--no-generate-normals')
            if args.no_generate_tangents:
                command.append('--no-generate-tangents')
            for remap in args.remap:
                command.extend(('--remap', remap))
            code = subprocess.run(command, check=False).returncode
            if code:
                return code
        return 0
    if not all((args.project, args.source, args.shader_root)):
        parser.error('Require project, source and shader-root, or --batch')
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
