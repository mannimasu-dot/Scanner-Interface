[CmdletBinding()]
param(
	[string]$Source,
	[string]$OutputDirectory,
	[string[]]$BitmapNames = @('#145', '#352', '#353', '#364', '#365', '#418', '#430', '#446')
)
$ErrorActionPreference = 'Stop'
$scriptDirectory = $PSScriptRoot
if (-not $scriptDirectory) { $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path }
$root = Split-Path -Parent $scriptDirectory
if (-not $Source) { $Source = Join-Path $root 'Infos\Scanner-Interface.exe' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $root 'Assets' }
$Source = (Resolve-Path -LiteralPath $Source).Path
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$sourceHash = (Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash

# Only map resource data; never load the executable as executable code.
if (-not ('OriginalAssetExtraction.Reader' -as [type])) {
	Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace OriginalAssetExtraction
{
	public sealed class ImageInfo
	{
		public int Id, Width, Height, BitsPerPixel, Bytes;
	}
	public sealed class ResourceInfo
	{
		public string Type, Name, File;
		public ushort Language;
		public int Width, Height, BitsPerPixel;
		public List<ImageInfo> Images = new List<ImageInfo>();
	}
	internal sealed class Key
	{
		public int Id = -1;
		public string Text;
		public string Label { get { return Id >= 0 ? "#" + Id : Text; } }
		public IntPtr Allocate() { return Id >= 0 ? new IntPtr(Id) : Marshal.StringToHGlobalUni(Text); }
		public void Release(IntPtr p) { if (Id < 0) Marshal.FreeHGlobal(p); }
		public static Key FromPointer(IntPtr p)
		{
			long n = p.ToInt64();
			return (n >= 0 && n <= 65535) ? new Key { Id = (int)n } : new Key { Text = Marshal.PtrToStringUni(p) };
		}
	}
	public static class Reader
	{
		private delegate bool NameCallback(IntPtr module, IntPtr type, IntPtr name, IntPtr parameter);
		private delegate bool LanguageCallback(IntPtr module, IntPtr type, IntPtr name, ushort language, IntPtr parameter);
		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
		private static extern IntPtr LoadLibraryExW(string path, IntPtr file, uint flags);
		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern bool FreeLibrary(IntPtr module);
		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
		private static extern bool EnumResourceNamesW(IntPtr module, IntPtr type, NameCallback callback, IntPtr parameter);
		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
		private static extern bool EnumResourceLanguagesW(IntPtr module, IntPtr type, IntPtr name, LanguageCallback callback, IntPtr parameter);
		[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
		private static extern IntPtr FindResourceExW(IntPtr module, IntPtr type, IntPtr name, ushort language);
		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern uint SizeofResource(IntPtr module, IntPtr resource);
		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
		[DllImport("kernel32.dll", SetLastError = true)]
		private static extern IntPtr LockResource(IntPtr resource);

		private static List<Key> Names(IntPtr module, int type)
		{
			var names = new List<Key>();
			NameCallback callback = delegate(IntPtr m, IntPtr t, IntPtr n, IntPtr p) { names.Add(Key.FromPointer(n)); return true; };
			if (!EnumResourceNamesW(module, new IntPtr(type), callback, IntPtr.Zero))
			{
				int error = Marshal.GetLastWin32Error();
				if (error != 1813 && error != 1814) throw new Win32Exception(error);
			}
			return names;
		}
		private static List<ushort> Languages(IntPtr module, int type, Key key)
		{
			var languages = new List<ushort>();
			IntPtr name = key.Allocate();
			try
			{
				LanguageCallback callback = delegate(IntPtr m, IntPtr t, IntPtr n, ushort l, IntPtr p) { languages.Add(l); return true; };
				if (!EnumResourceLanguagesW(module, new IntPtr(type), name, callback, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
			}
			finally { key.Release(name); }
			return languages;
		}
		private static byte[] Read(IntPtr module, int type, Key key, ushort language)
		{
			IntPtr name = key.Allocate();
			try
			{
				IntPtr resource = FindResourceExW(module, new IntPtr(type), name, language);
				if (resource == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
				uint size = SizeofResource(module, resource);
				if (size == 0 || size > int.MaxValue) throw new InvalidDataException("Invalid resource size: " + key.Label);
				IntPtr data = LockResource(LoadResource(module, resource));
				if (data == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
				var bytes = new byte[(int)size];
				Marshal.Copy(data, bytes, 0, bytes.Length);
				return bytes;
			}
			finally { key.Release(name); }
		}
		private static string Stem(Key key)
		{
			// Hex-encode named keys to prevent filename collisions and invalid path characters.
			if (key.Id >= 0) return key.Id.ToString();
			var text = new System.Text.StringBuilder("Name");
			foreach (char c in key.Text) text.Append(((int)c).ToString("X4"));
			return text.ToString();
		}
		private static void BitmapInfo(byte[] dib, ResourceInfo info, out int pixelOffset)
		{
			if (dib.Length < 12) throw new InvalidDataException("Truncated DIB");
			uint header = BitConverter.ToUInt32(dib, 0);
			int colors;
			if (header == 12)
			{
				info.Width = BitConverter.ToUInt16(dib, 4);
				info.Height = BitConverter.ToUInt16(dib, 6);
				info.BitsPerPixel = BitConverter.ToUInt16(dib, 10);
				colors = info.BitsPerPixel <= 8 ? 1 << info.BitsPerPixel : 0;
				pixelOffset = checked(12 + colors * 3);
			}
			else if (header >= 40 && header <= dib.Length)
			{
				info.Width = BitConverter.ToInt32(dib, 4);
				info.Height = Math.Abs(BitConverter.ToInt32(dib, 8));
				info.BitsPerPixel = BitConverter.ToUInt16(dib, 14);
				uint used = BitConverter.ToUInt32(dib, 32);
				colors = used != 0 ? checked((int)used) : (info.BitsPerPixel <= 8 ? 1 << info.BitsPerPixel : 0);
				uint compression = BitConverter.ToUInt32(dib, 16);
				int masks = header == 40 ? (compression == 3 ? 12 : compression == 6 ? 16 : 0) : 0;
				pixelOffset = checked((int)header + masks + colors * 4);
			}
			else throw new InvalidDataException("Unsupported DIB header: " + header);
			if (pixelOffset > dib.Length) throw new InvalidDataException("Truncated DIB palette");
		}
		public static List<ResourceInfo> Extract(string source, string output, string[] bitmapNames)
		{
			// LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE, no executable loading.
			IntPtr module = LoadLibraryExW(source, IntPtr.Zero, 0x00000002 | 0x00000020);
			if (module == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
			try
			{
				Directory.CreateDirectory(output);
				var result = new List<ResourceInfo>();
				bool primary = true;
				foreach (Key key in Names(module, 14))
				foreach (ushort language in Languages(module, 14, key))
				{
					byte[] group = Read(module, 14, key, language);
					if (group.Length < 6 || BitConverter.ToUInt16(group, 0) != 0 || BitConverter.ToUInt16(group, 2) != 1)
						throw new InvalidDataException("Invalid icon group: " + key.Label);
					int count = BitConverter.ToUInt16(group, 4);
					if (count == 0 || group.Length < 6 + count * 14) throw new InvalidDataException("Truncated icon group");
					var info = new ResourceInfo { Type = "RT_GROUP_ICON", Name = key.Label, Language = language };
					string file = primary ? "OriginalApp.ico" : "Original-Icon-" + Stem(key) + "-lang" + language + ".ico";
					primary = false;
					info.File = file;
					var images = new List<byte[]>();
					for (int i = 0; i < count; i++)
					{
						int entry = 6 + i * 14;
						int id = BitConverter.ToUInt16(group, entry + 12);
						byte[] image = Read(module, 3, new Key { Id = id }, language);
						if (image.Length != BitConverter.ToUInt32(group, entry + 8)) throw new InvalidDataException("Icon size mismatch: " + id);
						images.Add(image);
						info.Images.Add(new ImageInfo { Id = id, Width = group[entry] == 0 ? 256 : group[entry], Height = group[entry + 1] == 0 ? 256 : group[entry + 1], BitsPerPixel = BitConverter.ToUInt16(group, entry + 6), Bytes = image.Length });
					}
					using (var stream = new MemoryStream())
					using (var writer = new BinaryWriter(stream))
					{
						writer.Write((ushort)0); writer.Write((ushort)1); writer.Write((ushort)count);
						int offset = 6 + count * 16;
						for (int i = 0; i < count; i++)
						{
							writer.Write(group, 6 + i * 14, 8);
							writer.Write(images[i].Length); writer.Write(offset);
							offset = checked(offset + images[i].Length);
						}
						foreach (byte[] image in images) writer.Write(image);
						File.WriteAllBytes(Path.Combine(output, file), stream.ToArray());
					}
					result.Add(info);
				}
				if (primary) throw new InvalidDataException("No RT_GROUP_ICON found");
				foreach (Key key in Names(module, 2))
				foreach (ushort language in Languages(module, 2, key))
				{
					byte[] dib = Read(module, 2, key, language);
					var info = new ResourceInfo { Type = "RT_BITMAP", Name = key.Label, Language = language };
					int pixelOffset;
					BitmapInfo(dib, info, out pixelOffset);
					if (Array.IndexOf(bitmapNames, key.Label) >= 0)
					{
						info.File = "Original-Bitmap-" + Stem(key) + "-lang" + language + ".bmp";
						using (var stream = new MemoryStream())
						using (var writer = new BinaryWriter(stream))
						{
							writer.Write((ushort)0x4D42); writer.Write(checked(14 + dib.Length));
							writer.Write(0); writer.Write(checked(14 + pixelOffset)); writer.Write(dib);
							File.WriteAllBytes(Path.Combine(output, info.File), stream.ToArray());
						}
					}
					result.Add(info);
				}
				foreach (Key key in Names(module, 241))
				foreach (ushort language in Languages(module, 241, key))
				{
					byte[] toolbar = Read(module, 241, key, language);
					var info = new ResourceInfo { Type = "RT_TOOLBAR", Name = key.Label, Language = language };
					if (toolbar.Length >= 8) { info.Width = BitConverter.ToUInt16(toolbar, 2); info.Height = BitConverter.ToUInt16(toolbar, 4); }
					result.Add(info);
				}
				return result;
			}
			finally { FreeLibrary(module); }
		}
	}
}
'@
}
$resources = [OriginalAssetExtraction.Reader]::Extract($Source, $OutputDirectory, $BitmapNames)
if ((Get-FileHash -LiteralPath $Source -Algorithm SHA256).Hash -ne $sourceHash) {
	throw 'Source file changed during extraction.'
}
$report = [ordered]@{
	Source = $Source
	SHA256 = $sourceHash
	Method = 'LoadLibraryExW: LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE (0x22)'
	PrimarySelection = 'First RT_GROUP_ICON in native resource enumeration order; first language'
	Resources = @($resources)
}
$reportPath = Join-Path $OutputDirectory 'OriginalAssets.json'
[System.IO.File]::WriteAllText($reportPath, ($report | ConvertTo-Json -Depth 8), [System.Text.UTF8Encoding]::new($false))
$resources | Select-Object Type, Name, Language, Width, Height, BitsPerPixel, File | Format-Table -AutoSize
foreach ($resource in $resources) {
	if ($resource.Type -eq 'RT_GROUP_ICON') {
		Write-Output ("{0}: {1}, language {2}" -f $resource.File, $resource.Name, $resource.Language)
		$resource.Images | Format-Table Id, Width, Height, BitsPerPixel, Bytes -AutoSize
	}
}
Write-Output "Report: $reportPath"
