param([string]$Configuration = 'Release', [string]$Source = 'TW-Brother MFC-J6935DW LAN')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$directory = Join-Path $root ('TestResults\Hardware-' + [guid]::NewGuid().ToString('N'))
New-Item $directory -ItemType Directory | Out-Null
$exe = Join-Path $root ('artifacts\' + $Configuration + '\x64\Scanner.exe')
function Invoke-Bounded([string]$Name, [string[]]$Arguments, [int]$Seconds = 120) {
	$process = Start-Process $exe -ArgumentList $Arguments -PassThru
	if (-not $process.WaitForExit($Seconds * 1000)) { $process.Kill(); throw "$Name Zeitlimit" }
	[pscustomobject]@{ Name = $Name; ExitCode = $process.ExitCode }
}
$results = @()
$results += Invoke-Bounded 'Capabilities' @('--twain-capabilities', ('"' + $Source + '"'), ('"' + (Join-Path $directory 'capabilities.txt') + '"'))
$results += Invoke-Bounded 'DeviceEvents' @('--device-events', ('"' + (Join-Path $directory 'events.txt') + '"'))
foreach ($mode in @('memory', 'native', 'adf', 'duplex')) {
	$target = Join-Path $directory $mode
	$results += Invoke-Bounded $mode @('--hardware-scan-test', ('"' + $Source + '"'), ('"' + $target + '"'), $mode)
}
$print = Join-Path $directory 'print-test.pdf'
$results += Invoke-Bounded 'PrintToPdf' @('--print-file-test', '"Microsoft Print to PDF"', ('"' + $print + '"')) 60
$results | ConvertTo-Json | Set-Content (Join-Path $directory 'summary.json') -Encoding UTF8
Add-Type -AssemblyName System.Drawing
foreach ($mode in @('memory','native')) {
	$images = @(Get-ChildItem (Join-Path $directory $mode) -Filter '*.png')
	if (-not $images.Count) { throw "Kein wirklich erworbenes Bild fuer $mode" }
	foreach ($file in $images) { $image = [System.Drawing.Image]::FromFile($file.FullName); try { if ($image.Width -le 0 -or $image.Height -le 0) { throw 'Ungueltige Pixel' }; Write-Output "$mode $($image.Width)x$($image.Height) $($image.HorizontalResolution)x$($image.VerticalResolution) DPI" } finally { $image.Dispose() } }
}
if (-not (Test-Path $print)) { throw 'Keine Druckdatei erzeugt' }
$header = [System.IO.File]::ReadAllBytes($print)
if ($header.Length -lt 100 -or [System.Text.Encoding]::ASCII.GetString($header,0,5) -ne '%PDF-') { throw 'Druckdatei ist kein PDF' }
$results | Format-Table
Write-Output "Hardwareberichte: $directory"
if (@($results | Where-Object { $_.Name -in @('Capabilities','memory','native','PrintToPdf') -and $_.ExitCode -ne 0 }).Count) { throw 'Pflicht-Hardwarepruefung fehlgeschlagen' }
