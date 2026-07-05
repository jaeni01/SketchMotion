# e2e_mock.ps1 - MockBridge 상대 가상 통합 테스트 (하드웨어 불필요)
# 흐름: MockBridge(arm/agv) 기동 -> 앱 기동 -> 테스트이미지 트레이스 ->
#       Arm 드로잉 -> AGV 미션(EKF 폐루프) -> 패널 상태 텍스트로 합격 판정
param([string]$Root = (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))

$bin = Join-Path $Root "build\Release"
$fail = 0

Add-Type @'
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public class E2E {
  delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr p, EnumProc cb, IntPtr l);
  [DllImport("user32.dll")] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, StringBuilder l);
  [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  const uint WM_GETTEXT = 0x000D; const uint WM_GETTEXTLENGTH = 0x000E;
  public static string GetText(IntPtr h) {
    int len = (int)SendMessage(h, WM_GETTEXTLENGTH, IntPtr.Zero, IntPtr.Zero);
    var sb = new StringBuilder(len + 2);
    SendMessage(h, WM_GETTEXT, (IntPtr)(len + 1), sb);
    return sb.ToString();
  }
  // 모든 자손 창의 (hwnd, class, text) 수집
  public static List<Tuple<IntPtr,string,string>> Dump(IntPtr root) {
    var r = new List<Tuple<IntPtr,string,string>>();
    EnumChildWindows(root, (h, l) => {
      var cs = new StringBuilder(128); GetClassName(h, cs, 128);
      r.Add(Tuple.Create(h, cs.ToString(), GetText(h)));
      return true;
    }, IntPtr.Zero);
    return r;
  }
}
'@

function Fail($msg) { Write-Host "E2E FAIL: $msg" -ForegroundColor Red; $script:fail = 1 }
function Pass($msg) { Write-Host "E2E PASS: $msg" -ForegroundColor Green }

# 0) 이전 런 잔존 프로세스 제거 (포트 충돌/교차 오염 방지)
Stop-Process -Name MockBridge -Force -ErrorAction SilentlyContinue
Stop-Process -Name SketchMotion -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300

# 1) MockBridge 기동 (콘솔 출력 캡처)
$armP = Start-Process (Join-Path $bin "MockBridge.exe") -ArgumentList "arm" -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $env:TEMP "mock_arm.log")
$agvP = Start-Process (Join-Path $bin "MockBridge.exe") -ArgumentList "agv" -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput (Join-Path $env:TEMP "mock_agv.log")
Start-Sleep -Milliseconds 800

# 2) 앱 기동
Stop-Process -Name SketchMotion -Force -ErrorAction SilentlyContinue
Start-Process (Join-Path $bin "SketchMotion.exe")
Start-Sleep -Seconds 4
$app = Get-Process SketchMotion -ErrorAction SilentlyContinue
if (-not $app) { Fail "app did not start"; exit 1 }
$main = $app.MainWindowHandle

function FindPaneStatics {
    # Hardware 패널의 Static 텍스트들 (Arm:/AGV:/EKF:/진행)
    (
        [E2E]::Dump($main) | Where-Object { $_.Item2 -eq "Static" -and
            ($_.Item3 -like "Arm:*" -or $_.Item3 -like "AGV:*" -or $_.Item3 -like "EKF:*" -or
             $_.Item3 -like "*Idle*" -or $_.Item3 -like "*progress*" -or $_.Item3 -like "*DONE*" -or
             $_.Item3 -like "*Mission*" -or $_.Item3 -like "*Drawing*" -or $_.Item3 -like "*Stopped*" -or
             $_.Item3 -like "*FAULT*" -or $_.Item3 -like "*E-STOP*") }
    )
}
function PaneHwnd {
    # Hardware 버튼(Connect Arm)의 부모가 곧 패널
    $btn = [E2E]::Dump($main) | Where-Object { $_.Item3 -eq "Connect Arm" } | Select-Object -First 1
    if (-not $btn) { return [IntPtr]::Zero }
    Add-Type -Name P -Namespace W32 -MemberDefinition '[DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr h);'
    return [W32.P]::GetParent($btn.Item1)
}
function ClickPane([int]$cmdId) {
    $h = PaneHwnd
    if ($h -eq [IntPtr]::Zero) { Fail "hardware pane not found"; return }
    [E2E]::PostMessage($h, 0x0111, [IntPtr]$cmdId, [IntPtr]::Zero) | Out-Null  # WM_COMMAND
}
function WaitForText([string]$pattern, [int]$timeoutSec) {
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt $timeoutSec) {
        $hit = [E2E]::Dump($main) | Where-Object { $_.Item2 -eq "Static" -and $_.Item3 -like $pattern }
        if ($hit) { return $hit[0].Item3 }
        Start-Sleep -Milliseconds 500
    }
    return $null
}

# 3) 캔버스 채우기: 테스트 이미지 로드 + 트레이스 (기존 v1 명령)
[E2E]::PostMessage($main, 0x0111, [IntPtr]32785, [IntPtr]::Zero) | Out-Null; Start-Sleep -Milliseconds 800
[E2E]::PostMessage($main, 0x0111, [IntPtr]32784, [IntPtr]::Zero) | Out-Null; Start-Sleep -Seconds 2

# 4) 브리지 연결
ClickPane 32841  # Connect Arm
ClickPane 32842  # Connect AGV
$t = WaitForText "Arm: connected*" 10
if ($t) { Pass "arm connected ($t)" } else { Fail "arm connect timeout" }
$t = WaitForText "AGV: connected*" 10
if ($t) { Pass "agv connected ($t)" } else { Fail "agv connect timeout" }

# 5) 로봇 드로잉 (mock: 길이 기반 시뮬)
ClickPane 32843  # Draw on Robot
$t = WaitForText "Draw DONE*" 60
if ($t) { Pass "arm drawing completed" } else { Fail "arm drawing did not finish" }

# 6) AGV 미션 (EKF 폐루프: mock_pose -> EKF -> set_pose_estimate -> PathTracker)
ClickPane 32844  # AGV Mission
$t = WaitForText "Mission DONE*" 240
if ($t) { Pass "agv mission completed (closed loop)" } else {
    $now = (FindPaneStatics | ForEach-Object { $_.Item3 }) -join " | "
    Fail "agv mission did not finish. pane: $now"
}
# EKF가 실제로 돌았는지 (NIS 표시)
$ekf = [E2E]::Dump($main) | Where-Object { $_.Item2 -eq "Static" -and $_.Item3 -like "EKF: x=*NIS=*" }
if ($ekf) { Pass ("ekf running: " + $ekf[0].Item3) } else { Fail "ekf status not updating" }

# 7) 정리
Stop-Process -Name SketchMotion -Force -ErrorAction SilentlyContinue
Stop-Process -Id $armP.Id -Force -ErrorAction SilentlyContinue
Stop-Process -Id $agvP.Id -Force -ErrorAction SilentlyContinue

Write-Host "--- mock_agv.log ---"
Get-Content (Join-Path $env:TEMP "mock_agv.log") -ErrorAction SilentlyContinue | Select-Object -Last 12

if ($fail -eq 0) { Write-Host "E2E: ALL PASS" -ForegroundColor Green; exit 0 }
exit 1
