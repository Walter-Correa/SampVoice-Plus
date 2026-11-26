param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("Stage", "VerifyZip")]
    [string]$Action,

    [string]$RepoRoot = "",
    [string]$TargetDir = "",
    [string]$Version = "",
    [switch]$IncludeInstaller,
    [string]$ZipPath = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($RepoRoot)) {
    $RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
} else {
    $RepoRoot = (Resolve-Path $RepoRoot).Path
}

# Single source-of-truth mapping: 15 client language codes -> ASCII English filename labels
$LanguageMap = [ordered]@{
    "bg" = "Bulgarian"
    "de" = "German"
    "en" = "English"
    "es" = "Spanish"
    "fr" = "French"
    "hu" = "Hungarian"
    "id" = "Indonesian"
    "it" = "Italian"
    "pl" = "Polish"
    "pt" = "Portuguese"
    "ro" = "Romanian"
    "ru" = "Russian"
    "tr" = "Turkish"
    "uk" = "Ukrainian"
    "vi" = "Vietnamese"
}

function Normalize-ToCrlf([string]$Text) {
    return ($Text -replace "`r`n", "`n" -replace "`r", "`n" -replace "`n", "`r`n")
}

function Get-InstructionFileName([string]$Code, [string]$EnglishName) {
    return "$Code - $EnglishName.txt"
}

function Get-TechnicalTokensFromEnglish([string]$SourceFile) {
    $raw = [System.IO.File]::ReadAllText($SourceFile, [System.Text.Encoding]::UTF8)
    $tokens = New-Object System.Collections.Generic.List[string]

    # URLs
    foreach ($m in [regex]::Matches($raw, 'https?://[^\s\)\]>]+')) {
        $v = $m.Value.TrimEnd('.', ',')
        if (-not $tokens.Contains($v)) { $tokens.Add($v) }
    }

    # Technical paths, binaries, switches, product/SA-MP tokens
    $fixedPatterns = @(
        'SampVoice-Plus-Setup\.exe',
        'SampVoice-Plus-Client-Installer\.zip',
        'THIRD-PARTY-NOTICES\.txt',
        'sampvoice\.asi',
        'gta_sa\.exe',
        'dinput8\.dll',
        'bass\.dll',
        'bass_fx\.dll',
        'vorbisFile\.dll',
        'vorbisHooked\.dll',
        'scripts\\',
        'sampvoice-plus\\backup\\',
        'sampvoice-plus\\',
        'Documents\\GTA San Andreas User Files\\SAMP\\sampvoice\\',
        'Documents\\GTA San Andreas User Files\\sampvoice\\',
        '/LEGACY=remove',
        '/LEGACYCONFIG=remove',
        '\bF11\b',
        'SampVoice Plus',
        'SA-MP 0\.3\.7',
        '0\.3\.DL-R1',
        'DL-R1-2-MP',
        'R5-2-MP',
        'R4-2',
        'R3-1',
        'open\.mp'
    )
    foreach ($pat in $fixedPatterns) {
        $m = [regex]::Match($raw, $pat)
        if ($m.Success -and -not $tokens.Contains($m.Value)) {
            $tokens.Add($m.Value)
        }
    }
    return $tokens.ToArray()
}

if ($Action -eq "Stage") {
    if ([string]::IsNullOrWhiteSpace($TargetDir)) { throw "-TargetDir is required for Stage." }
    if ([string]::IsNullOrWhiteSpace($Version)) { throw "-Version is required for Stage." }

    $utf8Bom = New-Object System.Text.UTF8Encoding($true)
    $manualInstDir = Join-Path $TargetDir "Manual Install\Instructions"
    New-Item -ItemType Directory -Force -Path $manualInstDir | Out-Null

    $installerInstDir = $null
    if ($IncludeInstaller) {
        $installerInstDir = Join-Path $TargetDir "Installer\Instructions"
        New-Item -ItemType Directory -Force -Path $installerInstDir | Out-Null
    }

    foreach ($kv in $LanguageMap.GetEnumerator()) {
        $code = $kv.Key
        $engName = $kv.Value
        $outName = Get-InstructionFileName $code $engName

        $srcManual = Join-Path $RepoRoot "client\instructions\$code\Manual-Install.txt"
        if (-not (Test-Path $srcManual)) { throw "Missing source instruction file: $srcManual" }
        $manualContent = Normalize-ToCrlf (([System.IO.File]::ReadAllText($srcManual, [System.Text.Encoding]::UTF8)) -replace '@SVPLUS_VERSION@', $Version)
        [System.IO.File]::WriteAllText((Join-Path $manualInstDir $outName), $manualContent, $utf8Bom)

        if ($IncludeInstaller) {
            $srcInst = Join-Path $RepoRoot "client\instructions\$code\Installer.txt"
            if (-not (Test-Path $srcInst)) { throw "Missing source instruction file: $srcInst" }
            $instContent = Normalize-ToCrlf (([System.IO.File]::ReadAllText($srcInst, [System.Text.Encoding]::UTF8)) -replace '@SVPLUS_VERSION@', $Version)
            [System.IO.File]::WriteAllText((Join-Path $installerInstDir $outName), $instContent, $utf8Bom)
        }
    }
    Write-Host "Staged 15-language client instructions (IncludeInstaller=$IncludeInstaller)."
    exit 0
}

if ($Action -eq "VerifyZip") {
    if ([string]::IsNullOrWhiteSpace($ZipPath)) { throw "-ZipPath is required for VerifyZip." }
    Add-Type -AssemblyName System.IO.Compression.FileSystem

    $expected = New-Object System.Collections.Generic.List[string]
    $expected.Add("LICENSE")
    $expected.Add("THIRD-PARTY-NOTICES.txt")
    $expected.Add("Manual Install/sampvoice.asi")
    if ($IncludeInstaller) {
        $expected.Add("Installer/SampVoice-Plus-Setup.exe")
    }

    foreach ($kv in $LanguageMap.GetEnumerator()) {
        $outName = Get-InstructionFileName $kv.Key $kv.Value
        if ($outName -match '[^\x20-\x7E]') {
            throw "Instruction filename is not pure ASCII: $outName"
        }
        $expected.Add("Manual Install/Instructions/$outName")
        if ($IncludeInstaller) {
            $expected.Add("Installer/Instructions/$outName")
        }
    }

    $enManualTokens = Get-TechnicalTokensFromEnglish (Join-Path $RepoRoot "client\instructions\en\Manual-Install.txt")
    $enInstallerTokens = Get-TechnicalTokensFromEnglish (Join-Path $RepoRoot "client\instructions\en\Installer.txt")

    $resolvedZip = (Resolve-Path $ZipPath).Path
    $zip = [System.IO.Compression.ZipFile]::OpenRead($resolvedZip)
    try {
        $actual = @($zip.Entries | Where-Object { $_.Name -ne "" } | ForEach-Object { $_.FullName -replace '\\', '/' } | Sort-Object)
        $expSorted = @($expected | Sort-Object)
        if (($actual -join "`n") -ne ($expSorted -join "`n")) {
            throw "Zip mismatch for $ZipPath!`nExpected:`n$($expSorted -join "`n")`nActual:`n$($actual -join "`n")"
        }

        $instEntries = $zip.Entries | Where-Object {
            $p = ($_.FullName -replace '\\', '/')
            $_.Name -ne "" -and ($p -like "Installer/Instructions/*" -or $p -like "Manual Install/Instructions/*")
        }

        $checkedCount = 0
        foreach ($entry in $instEntries) {
            $entryPath = ($entry.FullName -replace '\\', '/')
            $ms = New-Object System.IO.MemoryStream
            $stream = $entry.Open()
            try {
                $stream.CopyTo($ms)
            } finally {
                $stream.Dispose()
            }
            $bytes = $ms.ToArray()
            $ms.Dispose()

            if ($bytes.Length -lt 3 -or $bytes[0] -ne 0xEF -or $bytes[1] -ne 0xBB -or $bytes[2] -ne 0xBF) {
                throw "Missing UTF-8 BOM in zip entry: $entryPath"
            }

            $text = [System.Text.Encoding]::UTF8.GetString($bytes, 3, $bytes.Length - 3)
            if ($text.Contains('@SVPLUS_VERSION@')) {
                throw "Unsubstituted @SVPLUS_VERSION@ placeholder in zip entry: $entryPath"
            }
            if (-not $text.Contains("`r`n") -or ($text -replace "`r`n", "").Contains("`n")) {
                throw "Non-CRLF line ending found in zip entry: $entryPath"
            }

            $reqTokens = if ($entryPath -like "Installer/*") { $enInstallerTokens } else { $enManualTokens }
            foreach ($tok in $reqTokens) {
                if (-not $text.Contains($tok)) {
                    throw "Zip entry '$entryPath' is missing required technical token '$tok'"
                }
            }
            ++$checkedCount
        }
        Write-Host "Verified $checkedCount instruction file(s) in $ZipPath (exact layout, UTF-8 BOM, CRLF, version substituted, technical tokens & URLs preserved)."
    } finally {
        $zip.Dispose()
    }
    exit 0
}
