$ErrorActionPreference = 'Stop'

Set-Location $PSScriptRoot

if (-not (Test-Path '.env')) {
  Copy-Item '.env.example' '.env'
  Write-Host "Created backend/.env from .env.example. Fill SUPABASE_URL, SUPABASE_ANON_KEY, GEMINI_API_KEY first."
  exit 1
}

Get-Content '.env' | ForEach-Object {
  $line = $_.Trim()
  if (-not $line -or $line.StartsWith('#')) {
    return
  }

  $parts = $line -split '=', 2
  if ($parts.Count -ne 2) {
    return
  }

  $name = $parts[0].Trim()
  $value = $parts[1].Trim().Trim('"')
  if ($name) {
    [System.Environment]::SetEnvironmentVariable($name, $value, 'Process')
  }
}

$cmake = Get-Command cmake -ErrorAction SilentlyContinue
if ($cmake) {
  cmake -S . -B build
  cmake --build build --config Release

  $exeCandidates = @(
    (Join-Path $PSScriptRoot 'build\Release\medicament_backend.exe'),
    (Join-Path $PSScriptRoot 'build\medicament_backend.exe')
  )

  foreach ($exe in $exeCandidates) {
    if (Test-Path $exe) {
      & $exe
      exit $LASTEXITCODE
    }
  }

  Write-Host 'Build completed, but medicament_backend.exe was not found.'
  exit 1
}

$gccPath = if (Test-Path 'C:\msys64\mingw64\bin\gcc.exe') {
  'C:\msys64\mingw64\bin\gcc.exe'
} else {
  $gccCmd = Get-Command gcc -ErrorAction SilentlyContinue
  if ($gccCmd) { $gccCmd.Source } else { $null }
}

if (-not $gccPath) {
  Write-Host 'No cmake and no gcc found. Install CMake or GCC first.'
  exit 1
}

$includeDirs = @(
  'C:\msys64\mingw64\include',
  'C:\MinGW\include'
)

$libDirs = @(
  'C:\msys64\mingw64\lib',
  'C:\MinGW\lib'
)

$curlHeader = $includeDirs | Where-Object { Test-Path (Join-Path $_ 'curl\curl.h') } | Select-Object -First 1
$cjsonHeaderDir = $includeDirs | Where-Object { (Test-Path (Join-Path $_ 'cjson\cJSON.h')) -or (Test-Path (Join-Path $_ 'cJSON.h')) } | Select-Object -First 1
$libDir = $libDirs | Where-Object {
  (Test-Path (Join-Path $_ 'libcurl.a')) -or (Test-Path (Join-Path $_ 'libcurl.dll.a'))
} | Select-Object -First 1

if (-not $curlHeader -or -not $cjsonHeaderDir -or -not $libDir) {
  Write-Host 'Missing libcurl/cJSON development files. Install one of these toolchains:'
  Write-Host '  1) MSYS2: pacman -S mingw-w64-x86_64-curl mingw-w64-x86_64-cjson'
  Write-Host '  2) vcpkg: vcpkg install curl cjson'
  exit 1
}

New-Item -ItemType Directory -Path (Join-Path $PSScriptRoot 'build') -Force | Out-Null
$exePath = Join-Path $PSScriptRoot 'build\medicament_backend.exe'

$includeFlags = @("-I$PSScriptRoot\include", "-I$curlHeader", "-I$cjsonHeaderDir")
$libFlags = @("-L$libDir")

if (Test-Path 'C:\msys64\mingw64\bin') {
  $env:Path = "C:\msys64\mingw64\bin;$env:Path"
}

$compileAttempts = @(
  @('src\main.c', 'src\app.c', '-o', $exePath, '-lcurl', '-lcjson', '-lws2_32'),
  @('src\main.c', 'src\app.c', '-o', $exePath, '-lcurl', '-lcJSON', '-lws2_32')
)

$built = $false
foreach ($args in $compileAttempts) {
  & $gccPath @includeFlags @libFlags @args
  if ($LASTEXITCODE -eq 0 -and (Test-Path $exePath)) {
    $built = $true
    break
  }
}

if (-not $built) {
  Write-Host 'GCC compilation failed. Verify libcurl and cJSON are installed for your compiler.'
  exit 1
}

& $exePath
exit $LASTEXITCODE
