param(
    [string]$Executable,
    [string]$PatternsHeader = (Join-Path (Split-Path -Parent $PSScriptRoot) 'references/fufu/FufuLauncher.UnlockerIsland__Patterns__Patterns.h'),
    [string]$Output,
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
if (-not ('Gimmd.PePatternInspector' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Globalization;
namespace Gimmd {
    public sealed class PatternMatch {
        public int FileOffset { get; set; }
        public uint Rva { get; set; }
    }
    public static class PePatternInspector {
        static void Need(byte[] b, long offset, long count) {
            if (offset < 0 || count < 0 || offset > b.LongLength - count)
                throw new ArgumentException("Invalid/truncated PE range");
        }
        public static PatternMatch[] Scan(byte[] b, string expression) {
            var tokens = expression.Split((char[])null, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length == 0 || tokens.Length > 4096) throw new ArgumentException("Invalid pattern length");
            var pattern = new int[tokens.Length];
            int anchor = -1;
            for (int i = 0; i < tokens.Length; ++i) {
                if (tokens[i] == "?" || tokens[i] == "??") pattern[i] = -1;
                else {
                    if (tokens[i].Length != 2) throw new ArgumentException("Invalid pattern byte");
                    pattern[i] = byte.Parse(tokens[i], NumberStyles.HexNumber, CultureInfo.InvariantCulture);
                    if (anchor < 0) anchor = i;
                }
            }
            if (anchor < 0) throw new ArgumentException("Pattern must have a fixed byte");
            Need(b, 0, 64);
            if (b[0] != 'M' || b[1] != 'Z') throw new ArgumentException("Missing MZ signature");
            int pe = BitConverter.ToInt32(b, 0x3c);
            Need(b, pe, 24);
            if (BitConverter.ToUInt32(b, pe) != 0x4550) throw new ArgumentException("Missing PE signature");
            if (BitConverter.ToUInt16(b, pe + 4) != 0x8664) throw new ArgumentException("Expected AMD64 PE");
            int sections = BitConverter.ToUInt16(b, pe + 6);
            int optional = BitConverter.ToUInt16(b, pe + 20);
            long table = (long)pe + 24 + optional;
            Need(b, table, (long)sections * 40);
            var matches = new List<PatternMatch>();
            for (int s = 0; s < sections; ++s) {
                int section = checked((int)(table + s * 40));
                if ((BitConverter.ToUInt32(b, section + 36) & 0x20000000u) == 0) continue;
                uint rva = BitConverter.ToUInt32(b, section + 12);
                long size = BitConverter.ToUInt32(b, section + 16);
                long start = BitConverter.ToUInt32(b, section + 20);
                Need(b, start, size);
                if (size < pattern.Length) continue;
                int last = checked((int)(start + size - pattern.Length));
                int cursor = checked((int)start);
                while (cursor <= last) {
                    int hit = Array.IndexOf(b, (byte)pattern[anchor], cursor + anchor, last - cursor + 1);
                    if (hit < 0) break;
                    int candidate = hit - anchor;
                    bool equal = true;
                    for (int j = 0; j < pattern.Length; ++j)
                        if (pattern[j] >= 0 && b[candidate + j] != pattern[j]) { equal = false; break; }
                    if (equal) {
                        matches.Add(new PatternMatch { FileOffset = candidate, Rva = checked(rva + (uint)(candidate - start)) });
                        if (matches.Count == 16) return matches.ToArray();
                    }
                    cursor = candidate + 1;
                }
            }
            return matches.ToArray();
        }
    }
}
'@
}
if ($SelfTest) {
    $sample = [byte[]]::new(1024)
    $sample[0] = 77; $sample[1] = 90
    [BitConverter]::GetBytes([int]128).CopyTo($sample, 0x3c)
    [BitConverter]::GetBytes([uint32]0x4550).CopyTo($sample, 128)
    [BitConverter]::GetBytes([uint16]0x8664).CopyTo($sample, 132)
    [BitConverter]::GetBytes([uint16]1).CopyTo($sample, 134)
    # One executable section at file offset 512, mapped at RVA 4096.
    [BitConverter]::GetBytes([uint32]4096).CopyTo($sample, 164)
    [BitConverter]::GetBytes([uint32]512).CopyTo($sample, 168)
    [BitConverter]::GetBytes([uint32]512).CopyTo($sample, 172)
    [BitConverter]::GetBytes([uint32]0x20000000).CopyTo($sample, 188)
    $sample[600] = 0xaa; $sample[601] = 0xbb; $sample[602] = 0xcc
    $selfHits = [Gimmd.PePatternInspector]::Scan($sample, 'AA ?? CC')
    if ($selfHits.Count -ne 1 -or $selfHits[0].FileOffset -ne 600 -or $selfHits[0].Rva -ne 4184) { throw 'Wildcard/RVA test failed.' }
    if ([Gimmd.PePatternInspector]::Scan($sample, '12 34').Count -ne 0) { throw 'Absent pattern test failed.' }
    $sample[700] = 0xaa; $sample[701] = 0xbb; $sample[702] = 0xcc
    if ([Gimmd.PePatternInspector]::Scan($sample, 'AA BB CC').Count -ne 2) { throw 'Multiple match test failed.' }
    $badRejected = $false
    try { [Gimmd.PePatternInspector]::Scan([byte[]]::new(8), 'AA') | Out-Null } catch { $badRejected = $true }
    if (-not $badRejected) { throw 'Truncated PE accepted.' }
    Write-Output 'Offline PE scanner: wildcard, RVA, absent, multiple and bounds checks passed.'
    if (-not $Executable) { return }
}
if (-not $Executable) { throw 'Specify -Executable or -SelfTest.' }
$source = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PatternsHeader).Path)
$imagePath = (Resolve-Path -LiteralPath $Executable).Path
$imageBytes = [IO.File]::ReadAllBytes($imagePath)
$wanted = @('FindGameObject','FindString','GetName','GameUpdate','ChangeFOV','FreeCamCameraGetMain','FreeCamComponentGetTransform','FreeCamTransformGetPosition','FreeCamTransformGetRotation','FreeCamTransformSetPosition','FreeCamTransformSetRotation')
$definitions = [regex]::Matches($source, 'inline\s+constexpr\s+const\s+char\*\s+(\w+)\s*=\s*((?:"[^"]*"\s*)+);')
$results = @(foreach ($definition in $definitions) {
    $name = $definition.Groups[1].Value
    if ($name -notin $wanted) { continue }
    $pattern = ([regex]::Matches($definition.Groups[2].Value, '"([^"]*)"') | ForEach-Object {$_.Groups[1].Value}) -join ' '
    $hits = [Gimmd.PePatternInspector]::Scan($imageBytes, $pattern)
    [ordered]@{name=$name;matchCount=$hits.Count;countCappedAt=16;status=$(if($hits.Count -eq 1){'unique-file-candidate'}elseif($hits.Count -eq 0){'not-found-in-file'}else{'ambiguous'});candidates=@($hits | ForEach-Object {[ordered]@{fileOffset=$_.FileOffset;rva=('0x{0:X}' -f $_.Rva)}})}
})
if ($results.Count -ne $wanted.Count) { throw 'Expected pattern definitions are missing.' }
$report = [ordered]@{
    schemaVersion=1;mode='offline-file-only';capturedAt=[DateTimeOffset]::Now.ToString('o')
    executable=$imagePath;sha256=(Get-FileHash -LiteralPath $imagePath -Algorithm SHA256).Hash
    patternsRepository='https://github.com/FufuLauncher/FufuLauncher.UnlockerIsland'
    patternsCommit='2433a01d4fb4f96d340e82d035750e55b6a2c534'
    note='File matches are unverified candidates, not callable runtime addresses. No process memory was accessed or modified.'
    patterns=$results
}
$json = $report | ConvertTo-Json -Depth 8
if ($Output) { $json | Set-Content -LiteralPath $Output -Encoding utf8 } else { $json }
