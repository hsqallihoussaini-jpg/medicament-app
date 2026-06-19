Set-Location (Join-Path $PSScriptRoot 'frontend')

if (-not (Test-Path '.env')) {
  Copy-Item '.env.example' '.env'
}

if (-not (Test-Path 'node_modules')) {
  npm install
}

npm run dev
