param([string]$Output = (Join-Path (Split-Path -Parent $PSScriptRoot) 'TestResults\UI-Reference-OCR.txt'))
$ErrorActionPreference='Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Runtime.WindowsRuntime
[void][Windows.Storage.StorageFile,Windows.Storage,ContentType=WindowsRuntime]
[void][Windows.Storage.Streams.IRandomAccessStream,Windows.Storage.Streams,ContentType=WindowsRuntime]
[void][Windows.Graphics.Imaging.BitmapDecoder,Windows.Foundation,ContentType=WindowsRuntime]
[void][Windows.Graphics.Imaging.SoftwareBitmap,Windows.Foundation,ContentType=WindowsRuntime]
[void][Windows.Media.Ocr.OcrEngine,Windows.Foundation,ContentType=WindowsRuntime]
[void][Windows.Media.Ocr.OcrResult,Windows.Foundation,ContentType=WindowsRuntime]
$m=[System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {$_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'} | Select-Object -First 1
function Await($operation,[Type]$type){$task=$m.MakeGenericMethod($type).Invoke($null,@($operation));$task.Wait();$task.Result}
$engine=[Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()
$text=[System.Text.StringBuilder]::new()
foreach($name in @('Action.jpg','Ansicht.jpg','Bearbeiten.jpg','Bearbeiten1.jpg','Extra.jpg','Hilfe.jpg','PanelMenu.jpg','Scanner.jpg','Programm.jpg')){
$file=Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync((Join-Path $root ('Infos\'+$name)))) ([Windows.Storage.StorageFile])
$stream=Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
try{$decoder=Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder]);$bitmap=Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap]);try{$result=Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult]);[void]$text.AppendLine("=== $name ==="); foreach($line in $result.Lines){[void]$text.AppendLine($line.Text)}}finally{$bitmap.Dispose()}}finally{$stream.Dispose()}
}
[System.IO.Directory]::CreateDirectory((Split-Path $Output))|Out-Null
[System.IO.File]::WriteAllText($Output,$text.ToString(),[System.Text.Encoding]::UTF8)
$text.ToString()
