'''Export selected public header closure and matching MSVC artifacts into a new SDK directory.'''
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from check_architecture import public_headers, license_inputs
from delivery_resources import validate_resources


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--library', type=Path, required=True)
    parser.add_argument('--dll', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--configuration', choices=('Debug', 'Release', 'Dist'), required=True)
    parser.add_argument('--resources', type=Path, required=True, help='Cooked Resources directory from the selected host')
    parser.add_argument('--tool-host', type=Path, required=True, help='Development host providing bounded --inspect')
    parser.add_argument('--package', type=Path, help='Optional validated package for the isolated SDK smoke host')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = (root / 'Catalpa/src').resolve()
    output = args.output.resolve()
    library = args.library.resolve(strict=True)
    dll = args.dll.resolve(strict=True)
    resources = args.resources.resolve(strict=True)
    tool_host = args.tool_host.resolve(strict=True)
    if output.exists() or library.suffix.lower() != '.lib' or dll.suffix.lower() != '.dll':
        parser.error('Require a new output directory and matching DLL/import library')
    headers = public_headers()
    vendors = [(root / 'Catalpa/third_party/glm/glm', 'glm'), (root / 'Catalpa/third_party/spdlog/include/spdlog', 'spdlog')]
    for directory, _ in vendors:
        if output.is_relative_to(directory.resolve()) or output.is_relative_to(source):
            parser.error('SDK output must not overlap header or dependency source directories')
        if not directory.is_dir() or any(file.is_symlink() for file in directory.rglob('*')):
            parser.error('Public dependency missing or contains symbolic links')
    if not resources.is_dir() or output.is_relative_to(resources) or any(p.is_symlink() for p in resources.rglob('*')):
        parser.error('Runtime resource directory missing, overlaps output, or contains links')
    if not (resources / 'Shaders/BuiltinShaders.manifest').is_file():
        parser.error('SDK requires cooked builtin shaders; Slang source is insufficient')
    validate_resources(resources)
    package = args.package.resolve(strict=True) if args.package else None
    if package and (package.suffix != '.catpkg' or
                    subprocess.run([str(tool_host), '--inspect', str(package)], check=False).returncode):
        parser.error('SDK smoke package inspection failed')
    licenses = license_inputs()
    output.mkdir(parents=True, exist_ok=False)
    for file, relative in headers.items():
        target = output / 'include' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
    for directory, name in vendors:
        shutil.copytree(directory, output / 'include' / name)
    shutil.copytree(resources, output / 'bin/Resources',
                    ignore=shutil.ignore_patterns('ShaderSources', '*.slang', '*.stamp'))
    validate_resources(output / 'bin/Resources', tool_host)
    for file, relative in ((library, 'lib/Catalpa.lib'), (dll, 'bin/Catalpa.dll')):
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(file, target)
    if package:
        shutil.copy2(package, output / 'bin/Smoke.catpkg')
        if subprocess.run([str(tool_host), '--inspect', str(output / 'bin/Smoke.catpkg')],
                          check=False).returncode:
            parser.error('Copied SDK smoke package inspection failed')
    for file in licenses:
        if file.is_file():
            target = output / 'licenses' / file.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, target)
    example = output / 'example'
    example.mkdir()
    (example / 'public_consumer.cpp').write_text('#include <Catalpa.h>\nint ConsumePublicHeader() { return sizeof(Catalpa::ApplicationStartupResult) > 0 ? 0 : 1; }\n', encoding='utf-8')
    (example / 'host.cpp').write_text('''#include <Catalpa.h>
#include <Core/Jobs/JobSystem.h>
#include <Assets/AssetManager.h>
#include <Assets/PackageReader.h>
extern int ConsumePublicHeader();
class SmokeLayer final : public Catalpa::Layer {
public:
    SmokeLayer() : Layer("SDK smoke") {}
    void OnAttach() override {
        std::string error;
        auto& application = Catalpa::Application::Get();
        const auto file = application.GetResourceRoot().parent_path() / "Smoke.catpkg";
        if (std::filesystem::exists(file)) {
            std::shared_ptr<const Catalpa::PackageReader> package;
            if (!Catalpa::PackageReader::Open(file, package, error)) {
                application.RequestClose(1);
                return;
            }
            packageEntry = package->EntryScene();
            package.reset();
            if (!application.StartPackage(file, error)) {
                application.RequestClose(1);
                return;
            }
        }
        if (!Catalpa::Application::Get().GetJobs().Submit([ready=ready](const Catalpa::CancellationToken& token) {
            if (!token.IsCancellationRequested()) ready->store(true);
        },job,error)) Catalpa::Application::Get().RequestClose(1);
    }
    void OnUpdate(Catalpa::Deltatime) override {
        ++frames;
        auto& application=Catalpa::Application::Get();
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(60)) {
            application.RequestClose(1);
            return;
        }
        if (packageEntry.HasValue()) {
            if (!retiringPackage) {
                const auto state = application.GetAssets().GetStatus(packageEntry);
                if (state == Catalpa::AssetStatus::Failed || state == Catalpa::AssetStatus::Cancelled) {
                    application.RequestClose(1);
                    return;
                }
                if (state != Catalpa::AssetStatus::Ready || ++packageFrames < 3) return;
                retiringPackage = true;
            }
            std::string error;
            if (!application.StopPackage(error)) return; // Busy retirement retries on the next frame.
            packageEntry = {};
        }
        if (frames>=3 && application.GetJobs().IsComplete(job))
            application.RequestClose(ready->load()?ConsumePublicHeader():1);
    }
    void OnDetach() override {
        job.Cancel();
        std::string error;
        if (!Catalpa::Application::Get().GetJobs().Wait(job,error))
            Catalpa::Application::Get().RequestClose(1);
    }
private:
    Catalpa::JobHandle job;
    std::shared_ptr<std::atomic_bool> ready=std::make_shared<std::atomic_bool>(false);
    unsigned frames=0, packageFrames=0;
    Catalpa::AssetId packageEntry;
    bool retiringPackage=false;
    std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
};
class SmokeApplication final : public Catalpa::Application {
public:
    SmokeApplication() { PushLayer(std::make_unique<SmokeLayer>()); }
};
Catalpa::Application* Catalpa::CreateApplication() { return new SmokeApplication(); }
Catalpa::ApplicationStartupResult Catalpa::HandleApplicationArguments(int, char**) { return {}; }
#include <Core/Entry.h>
''', encoding='utf-8')
    crt = '/MDd' if args.configuration == 'Debug' else '/MD'
    (example / 'build.cmd').write_text(f'''@echo off
setlocal
cd /d "%~dp0"
cl /nologo /std:c++20 /EHsc /utf-8 {crt} /DCATALPA_PLATFORM_WINDOW /I"../include" host.cpp public_consumer.cpp /Fe:"../bin/SdkSmoke.exe" /link /LIBPATH:"../lib" Catalpa.lib
if errorlevel 1 exit /b 1
"../bin/SdkSmoke.exe"
exit /b %errorlevel%
''', encoding='utf-8')
    manifest = {'schemaVersion': 1, 'compiler': 'MSVC v143', 'language': 'C++20', 'architecture': 'x64',
                'configuration': args.configuration, 'crt': '/MDd' if args.configuration == 'Debug' else '/MD',
                'defines': ['CATALPA_PLATFORM_WINDOW'], 'validation': 'exported; compile/link/run not performed',
                'resources': 'Included cooked resources and source notices; no source importer or SDK compiler tools',
                'files': {file.relative_to(output).as_posix(): hashlib.sha256(file.read_bytes()).hexdigest()
                          for file in sorted(output.rglob('*')) if file.is_file()}}
    (output / 'sdk-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
