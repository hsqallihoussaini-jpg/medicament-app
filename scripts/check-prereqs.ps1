$tools = @('node', 'npm', 'gcc', 'cmake')

Write-Host 'Tool availability:'
foreach ($tool in $tools) {
  $cmd = Get-Command $tool -ErrorAction SilentlyContinue
  if ($cmd) {
    Write-Host "  [OK] $tool -> $($cmd.Source)"
  } else {
    Write-Host "  [MISSING] $tool"
  }
}

$paths = @(
  'C:\msys64\mingw64\include\curl\curl.h',
  'C:\msys64\mingw64\include\cjson\cJSON.h',
  'C:\MinGW\include\curl\curl.h',
  'C:\MinGW\include\cjson\cJSON.h',
  'C:\MinGW\include\cJSON.h'
)

Write-Host ''
Write-Host 'Header probes:'
foreach ($p in $paths) {
  $state = if (Test-Path $p) { 'OK' } else { 'MISSING' }
  Write-Host "  [$state] $p"
}
