"""Read-only checks for dependency direction, public header closure and declared license evidence."""
import json
from pathlib import Path
import re
import hashlib


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'Catalpa/src'
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]', re.MULTILINE)


def public_headers():
    pending = [SOURCE / name for name in ('Catalpa.h', 'Core/Entry.h', 'Assets/AssetManager.h',
                                          'Assets/PackageReader.h', 'Core/Jobs/JobSystem.h')]
    result = {}
    while pending:
        file = pending.pop().resolve(strict=True)
        if file in result:
            continue
        if not file.is_relative_to(SOURCE.resolve()):
            raise ValueError(f'Header escapes public source root: {file}')
        relative = file.relative_to(SOURCE).as_posix()
        if relative.startswith(('Platform/', 'ECS/Detail/')):
            raise ValueError(f'Private/native header leaked into SDK: {relative}')
        text = file.read_text(encoding='utf-8-sig')
        if relative != 'Core/Entry.h' and re.search(r'\bint\s+main\s*\(', text):
            raise ValueError(f'Implicit entry definition: {relative}')
        result[file] = relative
        for include in INCLUDE.findall(text):
            for candidate in (file.parent / include, SOURCE / include):
                if candidate.is_file():
                    pending.append(candidate)
                    break
    return result


def license_inputs():
    lock = json.loads((ROOT / 'Dependencies.lock.json').read_text(encoding='utf-8'))
    result = {ROOT / 'ThirdPartyNotices.md'}
    for dependency in lock['dependencies']:
        directory = ROOT / dependency.get('path', 'Catalpa/third_party/' + dependency['name'])
        evidence = dependency.get('licenseEvidence', [])
        if dependency.get('license', {}).get('file'):
            evidence = evidence + [dependency['license']]
        for item in evidence:
            file = (directory / item['file']).resolve(strict=True)
            if not file.is_relative_to(directory.resolve()):
                raise ValueError('License path escapes its dependency')
            if item.get('sha256') and hashlib.sha256(file.read_bytes()).hexdigest() != item['sha256']:
                raise ValueError(f'License differs from recorded evidence: {file}')
            result.add(file)
    # 同时收集依赖目录中的许可声明，覆盖锁文件记录尚不完整的源码包。
    for directory in (ROOT / 'Catalpa/third_party').iterdir():
        if directory.is_dir():
            result.update(p for p in directory.iterdir() if p.is_file()
                          and p.name.lower().startswith(('license', 'copying', 'notice')))
    fonts = ROOT / 'Catalpa/Resources/Fonts'
    if fonts.is_dir():
        result.update(p for p in fonts.rglob('*') if p.is_file()
                      and p.name.lower().startswith(('license', 'ofl', 'notice')))
    return sorted(result)


def main():
    violations = []
    for directory in ('Runtime', 'Renderer', 'RHI', 'Scene', 'Assets'):
        for file in (SOURCE / directory).rglob('*'):
            if file.suffix not in ('.h', '.cpp'):
                continue
            for include in INCLUDE.findall(file.read_text(encoding='utf-8-sig')):
                if include.startswith(('Tools/', 'AssetPipeline/', 'ShaderCompiler/', 'CatalpaEditor/')):
                    violations.append(f'{file.relative_to(ROOT)}: runtime includes {include}')
                if directory in ('Renderer', 'RHI') and (include.startswith('Platform/Vulkan/')
                                                         or include.startswith(('vulkan/', 'glad/'))):
                    violations.append(f'{file.relative_to(ROOT)}: native backend leaked: {include}')
    if violations:
        raise ValueError('\n'.join(violations))
    headers = public_headers()
    licenses = license_inputs()
    print(f'Architecture: checked dependency directions; {len(headers)} public-closure headers; '
          f'{len(licenses)} existing notice files. No compilation or execution.')


if __name__ == '__main__':
    main()
