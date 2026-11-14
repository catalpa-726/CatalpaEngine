"""Export the current source snapshot and a file hash manifest into a new ZIP; never changes the checkout."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--with-dependencies', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    if output.exists() or output.suffix.lower() != '.zip':
        parser.error('Output must be a new .zip file')
    listed = subprocess.run(['git', 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
                            cwd=root, capture_output=True, check=True).stdout
    paths = set()
    for item in listed.decode('utf-8').split('\0'):
        if not item:
            continue
        path = root / item
        if path.is_file() and not item.startswith(('docx/', 'bin/', 'bin-int/', 'out/', 'CatalpaBox/assets/')):
            paths.add(path)
    if args.with_dependencies:
        paths.update(p for p in (root / 'Catalpa/third_party').rglob('*') if p.is_file()
                     and '.git' not in p.relative_to(root).parts and not p.name.endswith(('.obj', '.pdb')))
    for path in sorted(paths):
        if path.is_symlink() or not path.resolve().is_relative_to(root):
            parser.error(f'Source snapshot contains a link outside its root: {path}')
        if path.resolve() == output:
            parser.error('Output cannot overlap a selected source')
        if path.relative_to(root).as_posix().casefold() == 'source-manifest.json':
            parser.error('A source file collides with the generated snapshot manifest')
    output.parent.mkdir(parents=True, exist_ok=True)
    # 以独占方式创建新归档，验证失败也能保留已有归档。
    manifest = {}
    with zipfile.ZipFile(output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(paths):
            name = path.relative_to(root).as_posix()
            digest = hashlib.sha256()
            # 从同一次读取写入并计算哈希，避免并行修改源码使清单与ZIP内容不一致。
            with path.open('rb') as source, archive.open(name, 'w', force_zip64=True) as target:
                while block := source.read(1024 * 1024):
                    target.write(block)
                    digest.update(block)
            manifest[name] = digest.hexdigest()
        archive.writestr('source-manifest.json', json.dumps({'schemaVersion': 1,
                                                             'dependenciesIncluded': args.with_dependencies, 'files': manifest}, indent=2) + '\n')
    print(output)


if __name__ == '__main__':
    main()
