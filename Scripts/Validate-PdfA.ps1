param([Parameter(Mandatory=$true)][string]$Directory, [string]$JavaPath, [string]$VeraPdfJar)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$Directory = (Resolve-Path $Directory).Path
if (-not $JavaPath) {
	$local = Get-ChildItem (Join-Path $root 'artifacts\tools\jre') -Filter java.exe -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
	if ($local) { $JavaPath = $local.FullName } else { $JavaPath = (Get-Command java.exe -ErrorAction SilentlyContinue).Source }
}
if (-not $VeraPdfJar) {
	$local = Get-ChildItem (Join-Path $root 'artifacts\tools\verapdf\bin') -Filter 'cli-*.jar' -ErrorAction SilentlyContinue | Sort-Object Name -Descending | Select-Object -First 1
	if ($local) { $VeraPdfJar = $local.FullName }
}
if (-not $JavaPath -or -not (Test-Path $JavaPath) -or -not $VeraPdfJar -or -not (Test-Path $VeraPdfJar)) {
	throw 'Java und veraPDF CLI fehlen. Lokale Werkzeuge unter artifacts\tools installieren oder -JavaPath und -VeraPdfJar angeben. PDF/A-Pruefung wird nicht uebersprungen.'
}
$JavaPath = (Resolve-Path $JavaPath).Path
$VeraPdfJar = (Resolve-Path $VeraPdfJar).Path
$work = Join-Path $Directory 'PdfA-validation'
New-Item -ItemType Directory -Force $work | Out-Null
function Invoke-Validation([string[]]$Files, [string]$Name, [bool]$ExpectNonCompliant = $false) {
	$xml = Join-Path $work "$Name.xml"
	$err = Join-Path $work "$Name.stderr.txt"
	$arguments = @('-jar', ('"' + $VeraPdfJar + '"'), '--format', 'xml', '--flavour', '1b') + @($Files | ForEach-Object { '"' + $_ + '"' })
	$process = [System.Diagnostics.Process]::new()
	$process.StartInfo.FileName = $JavaPath
	$process.StartInfo.Arguments = $arguments -join ' '
	$process.StartInfo.UseShellExecute = $false
	$process.StartInfo.CreateNoWindow = $true
	$process.StartInfo.RedirectStandardOutput = $true
	$process.StartInfo.RedirectStandardError = $true
	$process.StartInfo.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
	try {
		if (-not $process.Start()) { throw 'Java-Prozess konnte nicht gestartet werden.' }
		$output = $process.StandardOutput.ReadToEndAsync()
		$errors = $process.StandardError.ReadToEndAsync()
		if (-not $process.WaitForExit(60000)) { $process.Kill(); $process.WaitForExit(); throw "veraPDF-Zeitlimit: $Name" }
		[System.IO.File]::WriteAllText($xml, $output.GetAwaiter().GetResult(), [System.Text.UTF8Encoding]::new($false))
		[System.IO.File]::WriteAllText($err, $errors.GetAwaiter().GetResult(), [System.Text.UTF8Encoding]::new($false))
		if ($process.ExitCode -ne 0 -and -not ($ExpectNonCompliant -and $process.ExitCode -eq 1)) { throw "veraPDF-Prozessfehler $($process.ExitCode): $err" }
	} finally { $process.Dispose() }
	[xml]$result = Get-Content $xml -Raw -Encoding UTF8
	if (-not $result.report.batchSummary) { throw "veraPDF-Ergebnis unvollstaendig: $xml" }
	foreach ($field in @('failedToParse','encrypted','outOfMemory','veraExceptions')) {
		if ([int]$result.report.batchSummary.GetAttribute($field) -ne 0) { throw "veraPDF $field in $xml" }
	}
	return $result
}
$names = [System.Collections.Generic.List[string]]::new()
foreach ($mode in 0..2) { foreach ($quality in @(80,100)) { $names.Add("archive-mode-$mode-q$quality.pdf") } }
$names.Add('archive-empty-metadata.pdf')
$names.Add('archive-split_0001.pdf')
$names.Add('archive-split_0002.pdf')
foreach ($kind in 1..3) { $names.Add("archive-kind-$kind.pdf") }
$files = @($names | ForEach-Object { (Resolve-Path (Join-Path $Directory $_)).Path })
$report = [System.Collections.Generic.List[string]]::new()
$failures = 0
try {
	$result = Invoke-Validation $files 'positive'
	$jobs = @($result.report.jobs.job)
	if ($jobs.Count -ne $files.Count -or [int]$result.report.batchSummary.totalJobs -ne $files.Count) { throw 'veraPDF hat nicht alle angeforderten Dateien geprueft.' }
	foreach ($job in $jobs) {
		$name = [System.IO.Path]::GetFileName([string]$job.item.name)
		if (-not $names.Contains($name)) { throw "Unerwartete Validator-Datei: $name" }
		$validation = $job.validationReport
		if (-not $validation -or $validation.isCompliant -ne 'true' -or [int]$validation.details.failedRules -ne 0 -or [int]$validation.details.failedChecks -ne 0) {
			$failures++; $report.Add("FAIL veraPDF PDF/A-1b $name")
		} else { $report.Add("PASS veraPDF PDF/A-1b $name") }
	}
	if ([int]$result.report.batchSummary.validationReports.compliant -ne $files.Count) { throw 'Nicht alle PDF/A-Dateien sind konform.' }
} catch { $failures++; $report.Add("FAIL veraPDF positive batch: $_") }
try {
	$source = Join-Path $Directory 'archive-mode-0-q100.pdf'
	$bytes = [System.IO.File]::ReadAllBytes($source)
	$marker = '<pdfaid:conformance>B</pdfaid:conformance>'
	$offset = [System.Text.Encoding]::GetEncoding(28591).GetString($bytes).IndexOf($marker, [StringComparison]::Ordinal)
	if ($offset -lt 0) { throw 'PDF/A-Metadaten fuer Negativkontrolle fehlen.' }
	$bytes[$offset + '<pdfaid:conformance>'.Length] = [byte][char]'Z'
	$negative = Join-Path $work 'negative-conformance.pdf'
	[System.IO.File]::WriteAllBytes($negative, $bytes)
	$result = Invoke-Validation @($negative) 'negative' $true
	$jobs = @($result.report.jobs.job)
	if ($jobs.Count -ne 1 -or $jobs[0].validationReport.isCompliant -ne 'false' -or [int]$jobs[0].validationReport.details.failedRules -lt 1) { throw 'Validator akzeptiert die absichtlich ungueltige PDF/A-Konformitaetsangabe.' }
	$report.Add('PASS veraPDF rejects deliberately invalid PDF/A conformance metadata')
} catch { $failures++; $report.Add("FAIL veraPDF negative control: $_") }
$report.Add("TOTAL FAILURES: $failures")
$report | Set-Content (Join-Path $Directory 'independent-pdfa-results.txt') -Encoding UTF8
$report
if ($failures) { exit 1 }
