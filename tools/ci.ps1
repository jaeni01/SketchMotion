# ci.ps1 - 빌드 + 단위테스트 + 가상 통합(E2E) 전체 게이트
# 사용: powershell -ExecutionPolicy Bypass -File tools\ci.ps1
param([string]$Config = "Release")

$Root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

Write-Host "== [1/3] Build ($Config x64) =="
& $msbuild (Join-Path $Root "SketchMotion.sln") /p:Configuration=$Config /p:Platform=x64 /m /v:minimal /nologo
if ($LASTEXITCODE -ne 0) { Write-Host "CI FAIL: build" -ForegroundColor Red; exit 1 }

Write-Host "== [2/3] Unit tests =="
& (Join-Path $Root "build\$Config\Tests.exe")
if ($LASTEXITCODE -ne 0) { Write-Host "CI FAIL: unit tests" -ForegroundColor Red; exit 1 }

Write-Host "== [3/3] Mock E2E (virtual integration) =="
powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Root "tools\e2e_mock.ps1")
if ($LASTEXITCODE -ne 0) { Write-Host "CI FAIL: e2e" -ForegroundColor Red; exit 1 }

Write-Host "CI: ALL GREEN" -ForegroundColor Green
exit 0
