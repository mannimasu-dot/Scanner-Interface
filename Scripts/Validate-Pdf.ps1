param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
Add-Type -AssemblyName System.Drawing
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
[void][Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType=WindowsRuntime]
[void][Windows.Data.Pdf.PdfDocument, Windows.Data.Pdf, ContentType=WindowsRuntime]
[void][Windows.Data.Pdf.PdfPageRenderOptions, Windows.Data.Pdf, ContentType=WindowsRuntime]
$generic = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
$action = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and -not $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction' } | Select-Object -First 1
function Await($operation, [Type]$type) { $task = $generic.MakeGenericMethod($type).Invoke($null, @($operation)); $task.Wait(); return $task.Result }
$report = [System.Collections.Generic.List[string]]::new()
$failures = 0
foreach ($name in @('lossless.pdf','jpeg.pdf','other-lossless.pdf','page-mode-0.pdf','page-mode-1.pdf','page-mode-2.pdf')) {
	try {
		$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Join-Path $Directory $name))) ([Windows.Storage.StorageFile])
		$pdf = Await ([Windows.Data.Pdf.PdfDocument]::LoadFromFileAsync($file)) ([Windows.Data.Pdf.PdfDocument])
		$expectedCount = if ($name -eq 'lossless.pdf') { 2 } else { 1 }
		if ($pdf.PageCount -ne $expectedCount) { throw "Seitenzahl $($pdf.PageCount) statt $expectedCount" }
		$page = $pdf.GetPage(0)
		try {
			if ($name -like 'page-mode-*') {
				$expected = switch ($name) { 'page-mode-0.pdf' { @(793.70079,1122.51968) } 'page-mode-1.pdf' { @(1122.51968,793.70079) } 'page-mode-2.pdf' { @(1228.8,768.0) } }
				if ([Math]::Abs($page.Size.Width-$expected[0]) -gt 0.1 -or [Math]::Abs($page.Size.Height-$expected[1]) -gt 0.1) { throw "Seitengroesse $($page.Size.Width)x$($page.Size.Height) stimmt nicht" }
			}
			$renderPath = Join-Path $Directory ($name + '.render.png')
			[System.IO.File]::WriteAllBytes($renderPath, [byte[]]@())
			$renderFile = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($renderPath)) ([Windows.Storage.StorageFile])
			$stream = Await ($renderFile.OpenAsync([Windows.Storage.FileAccessMode]::ReadWrite)) ([Windows.Storage.Streams.IRandomAccessStream])
			try {
				$options = [Windows.Data.Pdf.PdfPageRenderOptions]::new()
				$options.DestinationWidth = 320; $options.DestinationHeight = 200
				$task = $action.Invoke($null, @($page.RenderToStreamAsync($stream, $options))); $task.Wait()
			} finally { $stream.Dispose() }
			$bitmap = [System.Drawing.Image]::FromFile($renderPath)
			try { if ($bitmap.Width -ne 320 -or $bitmap.Height -ne 200) { throw 'Renderergebnis hat falsche Abmessungen' } } finally { $bitmap.Dispose() }
			$report.Add("PASS Windows.Data.Pdf $name pages=$($pdf.PageCount) size=$($page.Size.Width)x$($page.Size.Height), rendered=320x200")
		} finally { $page.Dispose() }
	} catch { $failures++; $report.Add("FAIL $name : $_") }
}
$report.Add("TOTAL FAILURES: $failures")
$report | Set-Content (Join-Path $Directory 'independent-pdf-results.txt')
$report
if ($failures) { exit 1 }
