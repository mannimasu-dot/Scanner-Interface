param([ValidateSet('Debug','Release')][string]$Configuration = 'Release', [switch]$Test, [string]$JavaPath, [string]$VeraPdfJar)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$msbuild = (Get-Command MSBuild.exe -ErrorAction SilentlyContinue).Source
if (-not $msbuild) {
	$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
	$install = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -property installationPath
	$msbuild = Join-Path $install 'MSBuild\Current\Bin\MSBuild.exe'
}
$project = Join-Path $root 'Scanner.vcxproj'
$artifacts = Join-Path $root ('artifacts\' + $Configuration)
foreach ($platform in @('Win32', 'x64')) {
	$out = Join-Path $artifacts $platform
	$obj = Join-Path $root ('artifacts\obj\' + $Configuration + '\' + $platform)
	& $msbuild $project /m /t:Build "/p:Configuration=$Configuration" "/p:Platform=$platform" "/p:OutDir=$out\" "/p:IntDir=$obj\" /nologo
	if ($LASTEXITCODE -ne 0) { throw "Build fehlgeschlagen: $platform" }
}
$x86 = Join-Path $artifacts 'Win32\Scanner.exe'
$x64 = Join-Path $artifacts 'x64\Scanner.exe'
Copy-Item $x86 (Join-Path $artifacts 'x64\TwainHost.exe') -Force
if (Test-Path (Join-Path $artifacts 'Win32\Scanner.pdb')) { Copy-Item (Join-Path $artifacts 'Win32\Scanner.pdb') (Join-Path $artifacts 'x64\TwainHost.pdb') -Force }
if ($Test) {
	foreach ($platform in @('Win32', 'x64')) {
		$directory = Join-Path $root ('TestResults\' + $Configuration + '-' + $platform + '-' + [guid]::NewGuid().ToString('N'))
		$exe = Join-Path $artifacts ($platform + '\Scanner.exe')
		$process = Start-Process $exe -ArgumentList @('--self-test', ('"' + $directory + '"')) -PassThru
		if (-not $process.WaitForExit(180000)) { $process.Kill(); throw "Selbsttest Zeitlimit: $platform ($directory)" }
		Get-Content (Join-Path $directory 'results.txt')
		if ($process.ExitCode -ne 0) { throw "Tests fehlgeschlagen: $platform (Exit $($process.ExitCode))" }
		$uiDirectory = Join-Path $directory 'UI'
		$ui = Start-Process $exe -ArgumentList @('--ui-smoke-test', ('"' + $uiDirectory + '"')) -PassThru
		if (-not $ui.WaitForExit(60000)) { $ui.Kill(); throw "UI-Test Zeitlimit: $platform" }
		Get-Content (Join-Path $uiDirectory 'ui-results.txt')
		if ($ui.ExitCode -ne 0) { throw "UI-Tests fehlgeschlagen: $platform (Exit $($ui.ExitCode))" }
		& (Join-Path $PSScriptRoot 'Validate-Pdf.ps1') -Directory $directory
		if ($LASTEXITCODE -ne 0) { throw "Unabhaengige PDF-Pruefung fehlgeschlagen: $platform" }
		& (Join-Path $PSScriptRoot 'Validate-Images.ps1') -Directory $directory
		if ($LASTEXITCODE -ne 0) { throw "Unabhaengige Bildpruefung fehlgeschlagen: $platform" }
		& (Join-Path $PSScriptRoot 'Validate-PdfA.ps1') -Directory $directory -JavaPath $JavaPath -VeraPdfJar $VeraPdfJar
		if ($LASTEXITCODE -ne 0) { throw "Unabhaengige PDF/A-Pruefung fehlgeschlagen: $platform" }
	}
}
Write-Host "Bereit: $x64 (mit TwainHost.exe); x86: $x86"
