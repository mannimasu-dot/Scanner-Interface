param([string]$Output = (Join-Path (Split-Path -Parent $PSScriptRoot) 'TestResults\Hilfe-OCR.txt'))
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Storage.StorageFile, Windows.Storage, ContentType=WindowsRuntime]
[void][Windows.Storage.Streams.IRandomAccessStream, Windows.Storage.Streams, ContentType=WindowsRuntime]
[void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType=WindowsRuntime]
[void][Windows.Graphics.Imaging.SoftwareBitmap, Windows.Foundation, ContentType=WindowsRuntime]
[void][Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType=WindowsRuntime]
[void][Windows.Media.Ocr.OcrResult, Windows.Foundation, ContentType=WindowsRuntime]
$method = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
function Await($operation, [Type]$type) { $task = $method.MakeGenericMethod($type).Invoke($null, @($operation)); $task.Wait(); return $task.Result }
$engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
if (-not $engine) { throw 'Keine Windows-OCR-Sprache verfuegbar.' }
$pages = Get-ChildItem (Join-Path $root 'Infos\Hilfe') -Filter '*.jpg' | Sort-Object Name
$text = [System.Text.StringBuilder]::new()
foreach ($page in $pages) {
	$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($page.FullName)) ([Windows.Storage.StorageFile])
	$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
	try {
		$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
		$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
		try { $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult]); [void]$text.AppendLine("=== $($page.Name) ==="); [void]$text.AppendLine($result.Text); [void]$text.AppendLine() } finally { $bitmap.Dispose() }
	} finally { $stream.Dispose() }
}
[void][System.IO.Directory]::CreateDirectory((Split-Path -Parent $Output))
[System.IO.File]::WriteAllText($Output, $text.ToString(), [System.Text.Encoding]::UTF8)
Write-Output "$($pages.Count) Hilfeseiten erkannt: $Output"
