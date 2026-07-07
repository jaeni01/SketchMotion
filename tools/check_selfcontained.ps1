# check_selfcontained.ps1 - 헤더 자기충족성 검사 (프로그래밍 원칙 02편의 "황금률")
# "어떤 헤더든 그것 하나만 include해도 컴파일돼야 한다."
# Core/의 각 .h를 독립 번역 단위로 컴파일해 통과 여부를 보고한다. CI 게이트용.
param([string]$Dir = (Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) "Core"))

$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$tmp = Join-Path $env:TEMP ("selfcontain_" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

$headers = Get-ChildItem "$Dir\*.h" | Select-Object -ExpandProperty Name
foreach ($h in $headers) {
    "#include `"$Dir\$h`"" | Set-Content (Join-Path $tmp ("tu_" + ($h -replace '\.h$','') + ".cpp")) -Encoding utf8
}
$cmd = "call `"$vc`" >nul 2>&1 && cd /d `"$tmp`" && (for %f in (tu_*.cpp) do @(cl /nologo /std:c++20 /EHsc /c /utf-8 %f >nul 2>err.txt && echo PASS %f || (echo FAIL %f & type err.txt)))"
$out = cmd /c $cmd
$out | Write-Host
Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue

if ($out | Select-String '^FAIL') {
    Write-Host "SELF-CONTAINMENT: FAIL (헤더가 스스로 필요한 것을 include하지 않음)" -ForegroundColor Red
    exit 1
}
Write-Host "SELF-CONTAINMENT: all $($headers.Count) Core headers pass" -ForegroundColor Green
exit 0
