# 以管理员身份运行 PowerShell，将 hub_service / recon_service 注册为 Windows 服务并配置失败重启。
# 使用前请将路径改为你的安装目录（默认可指向 CMake 构建输出 bin）。

param(
  [string]$BinDir = "$(Split-Path -Parent $MyInvocation.MyCommand.Path)\..\build\bin",
  [string]$DisplayPrefix = "Camera3D"
)

$ErrorActionPreference = "Stop"

function Ensure-Admin {
  $id = [Security.Principal.WindowsIdentity]::GetCurrent()
  $p = New-Object Security.Principal.WindowsPrincipal($id)
  if (-not $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "请以管理员身份运行本脚本。"
  }
}

Ensure-Admin

if (-not (Test-Path $BinDir)) {
  throw "未找到 BinDir: $BinDir"
}

$hub = Join-Path $BinDir "hub_service.exe"
$rec = Join-Path $BinDir "recon_service.exe"

foreach ($p in @($hub, $rec)) {
  if (-not (Test-Path $p)) {
    throw "缺少可执行文件: $p"
  }
}

# 旧版曾注册 Camera3DDetect；检测已合并到重建规划，此处主动卸载遗留服务。
$legacyDetect = Get-Service -Name "Camera3DDetect" -ErrorAction SilentlyContinue
if ($null -ne $legacyDetect) {
  Write-Host "检测到已弃用服务 Camera3DDetect，正在删除..."
  if ($legacyDetect.Status -eq 'Running') { Stop-Service -Name "Camera3DDetect" -Force }
  sc.exe delete "Camera3DDetect" | Out-Null
  Start-Sleep -Seconds 2
}

$services = @(
  @{ Name = "Camera3DHub"; Bin = $hub; Desc = "3D 相机底层中枢（gRPC + 共享内存）" },
  @{ Name = "Camera3DRecon"; Bin = $rec; Desc = "重建服务（算法占位）" }
)

foreach ($s in $services) {
  $name = $s.Name
  $exe = $s.Bin
  $disp = "$DisplayPrefix $name"

  $exists = Get-Service -Name $name -ErrorAction SilentlyContinue
  if ($null -ne $exists) {
    Write-Host "已存在服务 $name，先删除..."
    if ($exists.Status -eq 'Running') { Stop-Service -Name $name -Force }
    sc.exe delete $name | Out-Null
    Start-Sleep -Seconds 2
  }

  # binPath= exe 全路径；type= own 独立进程
  $binPath = "`"$exe`""
  sc.exe create $name binPath= $binPath start= auto DisplayName= "$disp" | Out-Null
  if ($LASTEXITCODE -ne 0) { throw "sc create 失败: $name" }

  sc.exe description $name "$($s.Desc)" | Out-Null

  # 失败重启：1s 后重启，最多 3 次（可按需调整）
  sc.exe failure $name reset= 86400 actions= restart/1000/restart/1000/restart/1000 | Out-Null
  sc.exe failureflag $name 1 | Out-Null
}

Write-Host "注册完成。可执行: Start-Service Camera3DHub; Start-Service Camera3DRecon"
Write-Host "卸载：Stop-Service 后 sc delete Camera3DHub / Camera3DRecon"
