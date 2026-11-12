param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$report = [System.Collections.Generic.List[string]]::new()
$failures = 0
foreach ($format in @(0,1,2,3)) {
	try {
		$image = [System.Drawing.Bitmap]::new((Join-Path $Directory "roundtrip-$format.img"))
		try {
			if ($image.Width -ne 7 -or $image.Height -ne 5) { throw 'Falsche Bildabmessungen' }
			if ($format -ne 1) { for ($y=0; $y -lt 5; $y++) { for ($x=0; $x -lt 7; $x++) { $pixel=$image.GetPixel($x,$y); if ($pixel.R -ne 170 -or $pixel.G -ne 90 -or $pixel.B -ne (($y*7+$x)*4)) { throw "Pixelabweichung bei $x,$y" } } } }
			$report.Add("PASS System.Drawing independent decode format=$format 7x5" + $(if($format -eq 1){' (JPEG lossy)'}else{' exact pixels'}))
		} finally { $image.Dispose() }
	} catch { $failures++; $report.Add("FAIL image format=$format : $_") }
}
function TiffCompression([string]$Path) {
	$data = [System.IO.File]::ReadAllBytes($Path)
	if ($data[0] -ne 73 -or $data[1] -ne 73 -or [BitConverter]::ToUInt16($data,2) -ne 42) { throw 'Test erwartet Little-Endian-TIFF' }
	$offset = [BitConverter]::ToUInt32($data,4)
	if ($offset+2 -ge $data.Length) { throw 'IFD ausserhalb der Datei' }
	$entries = [BitConverter]::ToUInt16($data,$offset)
	for ($i=0; $i -lt $entries; $i++) {
		$entry = $offset+2+$i*12
		if ($entry+12 -gt $data.Length) { throw 'IFD unvollstaendig' }
		if ([BitConverter]::ToUInt16($data,$entry) -eq 259) { return [BitConverter]::ToUInt16($data,$entry+8) }
	}
	throw 'Compression-Tag fehlt'
}
$codecs = @(1,3,4,5,8)
for ($i=0; $i -lt 5; $i++) {
	try {
		$actual=TiffCompression (Join-Path $Directory "mono-$i.tif")
		if ($actual -ne $codecs[$i]) { throw "Tag259=$actual statt $($codecs[$i])" }
		$report.Add("PASS independent TIFF compression tag mono=$i value=$actual")
	} catch { $failures++; $report.Add("FAIL TIFF mono=$i : $_") }
}
foreach($i in @(0,3,4)) {
	try {
		$actual=TiffCompression (Join-Path $Directory "color-$i.tif")
		if ($actual -ne $codecs[$i]) { throw "Tag259=$actual statt $($codecs[$i])" }
		$report.Add("PASS independent TIFF compression tag color=$i value=$actual")
	} catch { $failures++; $report.Add("FAIL TIFF color=$i : $_") }
}
function PngIdat([string]$Path) {
	$data = [System.IO.File]::ReadAllBytes($Path)
	if ($data.Length -lt 8 -or [BitConverter]::ToString($data,0,8) -ne '89-50-4E-47-0D-0A-1A-0A') { throw 'PNG-Signatur fehlt' }
	$stream = [System.IO.MemoryStream]::new()
	try {
		$position = 8
		$ended = $false
		while ($position + 12 -le $data.Length) {
			$length = [System.Net.IPAddress]::NetworkToHostOrder([BitConverter]::ToInt32($data,$position))
			if ($length -lt 0 -or [long]$position + 12 + $length -gt $data.Length) { throw 'PNG-Chunk unvollstaendig' }
			$type = [System.Text.Encoding]::ASCII.GetString($data,$position+4,4)
			if ($type -eq 'IDAT') { $stream.Write($data,$position+8,$length) }
			$position += 12 + $length
			if ($type -eq 'IEND') { $ended = $true; break }
		}
		if (-not $ended -or $position -ne $data.Length -or $stream.Length -lt 6) { throw 'PNG-Abschluss oder Bilddaten fehlen' }
		return ,$stream.ToArray()
	} finally { $stream.Dispose() }
}
$pngSizes = @{}
foreach ($level in 0..9) {
	try {
		$path = Join-Path $Directory "png-level-$level.png"
		$idat = PngIdat $path
		$pngSizes[$level] = $idat.Length
		$expectedFlag = if ($level -le 1) { 0 } elseif ($level -le 5) { 1 } elseif ($level -eq 6) { 2 } else { 3 }
		if (($idat[0] -band 15) -ne 8 -or (($idat[0]*256+$idat[1]) % 31) -ne 0 -or ($idat[1] -shr 6) -ne $expectedFlag) { throw 'Zlib-Kompressionskennzeichnung falsch' }
		$image = [System.Drawing.Bitmap]::new($path)
		try {
			if ($image.Width -ne 257 -or $image.Height -ne 129 -or [Math]::Abs($image.HorizontalResolution-150) -gt .02 -or [Math]::Abs($image.VerticalResolution-96) -gt .02) { throw 'PNG-Masse oder asymmetrische DPI stimmen nicht' }
			for ($y=0; $y -lt 129; $y++) { for ($x=0; $x -lt 257; $x++) {
				$pixel=$image.GetPixel($x,$y)
				$r=([int][Math]::Floor($x/11)+[int][Math]::Floor($y/13))%2*255
				$g=($x*3+[int][Math]::Floor($y/5))%256
				$b=([int][Math]::Floor($x/7)+[int][Math]::Floor($y/3))%17*15
				if ($pixel.R -ne $r -or $pixel.G -ne $g -or $pixel.B -ne $b -or $pixel.A -ne 255) { throw "PNG-Pixelabweichung bei $x,$y" }
			} }
		} finally { $image.Dispose() }
		$report.Add("PASS independent PNG level=$level exact pixels, DPI and zlib flag; IDAT=$($idat.Length)")
	} catch { $failures++; $report.Add("FAIL PNG level=$level : $_") }
}
if ($pngSizes.ContainsKey(0) -and $pngSizes.ContainsKey(1) -and $pngSizes.ContainsKey(9) -and $pngSizes[0] -gt $pngSizes[1] -and $pngSizes[1] -gt $pngSizes[9]) {
	$report.Add('PASS independent PNG compression changes payload size for levels 0/1/9')
} else { $failures++; $report.Add('FAIL independent PNG compression payload-size comparison') }
$report.Add("TOTAL FAILURES: $failures")
$report | Set-Content (Join-Path $Directory 'independent-image-results.txt')
$report
if($failures){exit 1}
