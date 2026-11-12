param([string]$Printer = 'Brother MFC-J6935DW Printer', [string]$Source = 'TW-Brother MFC-J6935DW LAN')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$directory = Join-Path $root ('TestResults\AdfPrintCopy-' + [guid]::NewGuid().ToString('N'))
New-Item $directory -ItemType Directory | Out-Null
$exe = Join-Path $root 'artifacts\Release\x64\Scanner.exe'
$results = [System.Collections.Generic.List[object]]::new()
Get-CimInstance Win32_Printer | Where-Object { $_.Default -or $_.Name -eq $Printer } | Select-Object Name,Default,DriverName,PortName,WorkOffline | ConvertTo-Json | Set-Content (Join-Path $directory 'printers.json') -Encoding UTF8
$physical = Get-Printer -Name $Printer
if ($physical.PortName -match 'PORTPROMPT|FPR|FILE:') { throw 'Ein physischer Drucker ist erforderlich.' }
function Run([string]$Name, [string[]]$Arguments, [int]$Seconds = 360) {
	Write-Output "START $Name $(Get-Date -Format o)"
	$process = Start-Process $exe -ArgumentList $Arguments -PassThru
	if (-not $process.WaitForExit($Seconds * 1000)) { $process.Kill(); $results.Add([pscustomobject]@{Name=$Name;ExitCode=124}); Write-Output "TIMEOUT $Name" }
	else { $results.Add([pscustomobject]@{Name=$Name;ExitCode=$process.ExitCode}); Write-Output "END $Name Exit=$($process.ExitCode)" }
	$results | ConvertTo-Json | Set-Content (Join-Path $directory 'summary.json') -Encoding UTF8
}
function ObservePrint([string]$Name, [string]$Report) {
	if (-not (Test-Path $Report)) { return }
	$text = Get-Content $Report -Raw
	if ($text -notmatch 'JobId=(\d+)') { return }
	$jobId = [int]$Matches[1]
	$observations = @()
	for ($i = 0; $i -lt 12; ++$i) {
		$job = Get-PrintJob -PrinterName $Printer -ID $jobId -ErrorAction SilentlyContinue
		if (-not $job) { $observations += [pscustomobject]@{Time=(Get-Date -Format o);JobId=$jobId;State='NoLongerInQueue';PhysicalOutputVisuallyConfirmed=$false}; break }
		$observations += [pscustomobject]@{Time=(Get-Date -Format o);JobId=$jobId;State=[string]$job.JobStatus;PagesPrinted=$job.PagesPrinted;TotalPages=$job.TotalPages}
		Start-Sleep -Seconds 5
	}
	$observations | ConvertTo-Json | Set-Content (Join-Path $directory ($Name + '-queue.json')) -Encoding UTF8
}
$duplex = Join-Path $directory 'duplex'
Run 'Duplex' @('--hardware-scan-test', ('"'+$Source+'"'), ('"'+$duplex+'"'), 'duplex')
Run 'AdfSimplex' @('--hardware-scan-test', ('"'+$Source+'"'), ('"'+(Join-Path $directory 'adf')+'"'), 'adf')
$printReport = Join-Path $directory 'physical-print.txt'
Run 'PhysicalPrint' @('--physical-print-test', ('"'+$Printer+'"'), ('"'+$printReport+'"')) 120
ObservePrint 'physical-print' $printReport
$scanResult = Join-Path $duplex 'hardware-results.txt'
if ((Test-Path $scanResult) -and ((Get-Content $scanResult -Raw) -match 'Completed=1 Cancelled=0')) {
	$image = Get-ChildItem $duplex -Filter '*.png' | Sort-Object Name | Select-Object -First 1
	if ($image) { $copyReport = Join-Path $directory 'copy-print.txt'; Run 'ScanCopy' @('--print-image-test', ('"'+$Printer+'"'), ('"'+$image.FullName+'"'), ('"'+$copyReport+'"')) 120; ObservePrint 'copy-print' $copyReport }
} else { Write-Output 'COPY NOT PRINTED: no completed fresh duplex scan' }
Add-Type -AssemblyName System.Drawing
$pixels = @()
foreach ($file in (Get-ChildItem $duplex -Filter '*.png' -ErrorAction SilentlyContinue)) {
	$image = [System.Drawing.Image]::FromFile($file.FullName)
	try { $pixels += [pscustomobject]@{File=$file.Name;Width=$image.Width;Height=$image.Height;DpiX=$image.HorizontalResolution;DpiY=$image.VerticalResolution} } finally { $image.Dispose() }
}
$pixels | ConvertTo-Json | Set-Content (Join-Path $directory 'independent-images.json') -Encoding UTF8
Write-Output "Reports: $directory"
$results | Format-Table
