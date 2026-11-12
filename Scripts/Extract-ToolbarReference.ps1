$ErrorActionPreference='Stop'
$root = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.Drawing
$source=[System.Drawing.Bitmap]::new((Join-Path $root 'Infos\Programm.jpg'))
try {
 $areas=@{Scan=@(56,54,34,35);Save=@(165,54,34,35);Print=@(232,54,34,35);Mail=@(365,54,34,35);New=@(435,54,34,35);Edit=@(499,54,34,35);Symbols=@(566,54,37,35);Copy=@(866,54,35,35)}
 foreach($name in $areas.Keys){$r=$areas[$name];$bitmap=$source.Clone([System.Drawing.Rectangle]::new($r[0],$r[1],$r[2],$r[3]),[System.Drawing.Imaging.PixelFormat]::Format24bppRgb);try{$bitmap.Save((Join-Path $root ('Assets\Toolbar-'+$name+'.bmp')),[System.Drawing.Imaging.ImageFormat]::Bmp)}finally{$bitmap.Dispose()}}
}finally{$source.Dispose()}
