# Monta o pacote para jogar (ArenaRemake-PTBR-<versão>.zip): executável, SDL2, bibliotecas do Visual C++,
# pasta data e LEIA-ME. Não leva saves nem nenhum arquivo do Arena original.
#
# Uso (depois de compilar em Release):
#   powershell -ExecutionPolicy Bypass -File tools/pacote/empacotar.ps1 -Versao 2026.11.01 [-Build C:/arena-build/Release] [-Saida Z:/pacotes]
param(
    [Parameter(Mandatory = $true)][string]$Versao,
    [string]$Build = "C:/arena-build/Release",
    [string]$Saida = "."
)
$ErrorActionPreference = "Stop"
$repo = Resolve-Path "$PSScriptRoot/../.."

# Bibliotecas do Visual C++ (distribuição junto do programa, permitida pela Microsoft).
$vswhere = "${env:ProgramFiles(x86)}/Microsoft Visual Studio/Installer/vswhere.exe"
$vs = & $vswhere -latest -property installationPath
$crt = Get-ChildItem "$vs/VC/Redist/MSVC" -Recurse -Directory -Filter "Microsoft.VC*.CRT" |
    Where-Object { $_.FullName -match "\\x64\\" -and $_.FullName -notmatch "onecore|debug_nonredist" } | Select-Object -First 1
if (-not $crt) { throw "Bibliotecas do Visual C++ (Redist x64) não encontradas." }

$temp = Join-Path ([IO.Path]::GetTempPath()) "arena-pacote"
if (Test-Path $temp) { Remove-Item $temp -Recurse -Force }
$pasta = "$temp/ArenaRemake"
New-Item -ItemType Directory -Force "$pasta/data/mods", "$pasta/data/lang", "$pasta/data/exe" | Out-Null

Copy-Item "$Build/ArenaRemake.exe", "$Build/SDL2.dll" $pasta
Copy-Item "$($crt.FullName)/msvcp140.dll", "$($crt.FullName)/vcruntime140.dll", "$($crt.FullName)/vcruntime140_1.dll" $pasta
Copy-Item "$repo/data/lang/*" "$pasta/data/lang"
Copy-Item "$repo/data/exe/*" "$pasta/data/exe"
Copy-Item "$repo/data/settings.ini" "$pasta/data/settings.ini"
Copy-Item "$repo/data/settings.ini" "$pasta/settings.ini"
Copy-Item "$PSScriptRoot/LEIA-ME.txt" $pasta

$zip = Join-Path (Resolve-Path $Saida) "ArenaRemake-PTBR-$Versao.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $pasta -DestinationPath $zip
Remove-Item $temp -Recurse -Force
"Pacote: $zip ({0:N1} MB)" -f ((Get-Item $zip).Length / 1MB)
