<#
.SYNOPSIS
    Checks a portable release package from the outside, the way a user's machine would meet it.

.DESCRIPTION
    Unpacks the ZIP into a fresh temporary folder and checks, in this order:
      - the file name fits the launcher's release asset pattern, and the checksum file (if
        there is one) says the same hash as the ZIP,
      - the entry names are plain relative paths with "/" and the files the launcher needs are there,
      - update-manifest.json lists exactly the files in the ZIP, with the right size and SHA-256,
      - nothing that does not belong in a release (debug files, settings, logs),
      - the program holds no trace of the user who built it (name, profile folder),
      - the program's own --self-test passes from the unpacked folder (TLS, the built-in project
        list, hashing, decompression, a writable portable folder), and it writes nothing outside
        the folder it lies in,
      - the program's own --check-package accepts the ZIP as a self-update package.

    Exit code 0: all checks passed. 1: at least one failed.

.EXAMPLE
    .\scripts\verify-package.ps1 -Zip dist\KERJAMI-0.1.0-Windows-x64-Portable.zip
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Zip,

    # Keep the unpacked folder (printed at the end) for a look.
    [switch]$KeepFiles,

    # Also write the results as JSON (a list of {name, ok, detail}), for other scripts.
    [string]$ResultFile
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$results = New-Object System.Collections.Generic.List[object]
function Add-Result([string]$Name, [bool]$Ok, [string]$Detail) {
    $results.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
}

function Get-BrandValue([string]$Name) {
    $text = Get-Content -LiteralPath (Join-Path $repoRoot 'cmake\Branding.cmake') -Raw
    $match = [regex]::Match($text, 'set\(' + [regex]::Escape($Name) + '\s+"([^"]*)"')
    if (-not $match.Success) { throw "$Name not found in cmake\Branding.cmake." }
    return $match.Groups[1].Value
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Test-ContainsText([byte[]]$Bytes, [string]$Needle) {
    # The text as it may be stored in a program: 8-bit and UTF-16.
    $ascii = [System.Text.Encoding]::GetEncoding(28591).GetString($Bytes)
    if ($ascii.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) { return $true }
    $utf16 = [System.Text.Encoding]::Unicode.GetString($Bytes)
    return $utf16.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0
}

$Zip = [System.IO.Path]::GetFullPath($Zip)
if (-not (Test-Path -LiteralPath $Zip)) { throw "$Zip not found." }
$zipName = Split-Path -Leaf $Zip
$product = Get-BrandValue 'BRAND_PRODUCT_NAME'
$exeName = (Get-BrandValue 'BRAND_EXECUTABLE_NAME') + '.exe'

$work = Join-Path ([System.IO.Path]::GetTempPath()) ('verify-package-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$package = Join-Path $work 'package'
$elsewhere = Join-Path $work 'cwd'
New-Item -ItemType Directory -Path $package, $elsewhere -Force | Out-Null

try {
    # --- the name and the checksum file --------------------------------------------------------
    $pattern = '^' + [regex]::Escape($product) + '-(\d+\.\d+\.\d+)-Windows-x64-Portable\.zip$'
    $nameMatch = [regex]::Match($zipName, $pattern)
    Add-Result 'file name' $nameMatch.Success "expected $product-<major.minor.patch>-Windows-x64-Portable.zip, got $zipName"
    $version = if ($nameMatch.Success) { $nameMatch.Groups[1].Value } else { '' }
    $zipHash = Get-Sha256 $Zip

    $checksumFile = $Zip + '.sha256'
    if (Test-Path -LiteralPath $checksumFile) {
        $line = (Get-Content -LiteralPath $checksumFile -TotalCount 1)
        $sumMatch = [regex]::Match($line, '^([0-9a-fA-F]{64})\s+\*?(.+)$')
        $sumOk = $sumMatch.Success -and $sumMatch.Groups[1].Value.ToLowerInvariant() -eq $zipHash -and $sumMatch.Groups[2].Value.Trim() -eq $zipName
        Add-Result 'checksum file' $sumOk "$(Split-Path -Leaf $checksumFile): one line '<hash>  <name>' that fits the ZIP"
    } else {
        Add-Result 'checksum file' $false "$(Split-Path -Leaf $checksumFile) is missing; the release needs it next to the ZIP"
    }

    # --- the entries ---------------------------------------------------------------------------------
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $entryNames = @($archive.Entries | ForEach-Object { $_.FullName })
    } finally { $archive.Dispose() }
    $badNames = @($entryNames | Where-Object { $_ -match '\\' -or $_.StartsWith('/') -or $_ -match '(^|/)\.\.(/|$)' -or $_ -match '^[A-Za-z]:' -or $_.EndsWith('/') })
    Add-Result 'entry names' ($badNames.Count -eq 0) $(if ($badNames.Count -eq 0) { "$($entryNames.Count) entries, plain relative paths with /" } else { 'unsafe: ' + ($badNames -join ', ') })
    Add-Result 'required files' (($entryNames -contains $exeName) -and ($entryNames -contains 'update-manifest.json')) "$exeName and update-manifest.json at the root"
    if ($badNames.Count -gt 0) { throw 'Unsafe entry names: nothing is unpacked.' }

    [System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $package)

    # --- the manifest ----------------------------------------------------------------------------------
    $manifest = Get-Content -LiteralPath (Join-Path $package 'update-manifest.json') -Raw -Encoding UTF8 | ConvertFrom-Json
    Add-Result 'manifest header' (($manifest.schema_version -eq 1) -and ($manifest.product -eq $product) -and ($manifest.version -eq $version) -and ($manifest.launcher -eq $exeName)) `
        "schema 1, product $product, version $version, launcher $exeName (manifest says: schema $($manifest.schema_version), $($manifest.product), $($manifest.version), $($manifest.launcher))"

    $problems = New-Object System.Collections.Generic.List[string]
    $listed = @{}
    foreach ($file in $manifest.files) {
        $listed[$file.path.ToLowerInvariant()] = $true
        $path = Join-Path $package ($file.path.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $path)) { $problems.Add("$($file.path) is listed but not in the ZIP"); continue }
        if ((Get-Item -LiteralPath $path).Length -ne [int64]$file.size) { $problems.Add("$($file.path) has another size") }
        if ((Get-Sha256 $path) -ne $file.sha256.ToLowerInvariant()) { $problems.Add("$($file.path) has another SHA-256") }
    }
    $packagePrefix = $package.TrimEnd('\') + '\'
    foreach ($file in (Get-ChildItem -LiteralPath $package -Recurse -File)) {
        $relative = $file.FullName.Substring($packagePrefix.Length).Replace('\', '/')
        if ($relative -eq 'update-manifest.json') { continue }
        if (-not $listed.ContainsKey($relative.ToLowerInvariant())) { $problems.Add("$relative is in the ZIP but not in the manifest") }
    }
    Add-Result 'manifest lists the files' ($problems.Count -eq 0) $(if ($problems.Count -eq 0) { "$(@($manifest.files).Count) files, size and SHA-256 match, nothing unlisted" } else { $problems -join '; ' })

    # --- nothing that does not belong in a release -----------------------------------------------------------
    $unwanted = @(Get-ChildItem -LiteralPath $package -Recurse -File | Where-Object { $_.Extension -match '^\.(pdb|ilk|obj|o|a|lib|exp|log|tmp|part)$' -or $_.Name -match '^(settings|launcher)\.json$|\.lock$' } | ForEach-Object { $_.Name })
    Add-Result 'no development leftovers' ($unwanted.Count -eq 0) $(if ($unwanted.Count -eq 0) { 'no debug files, settings, logs or locks' } else { $unwanted -join ', ' })

    # --- no trace of whoever built it -------------------------------------------------------------------------------
    $traces = New-Object System.Collections.Generic.List[string]
    $needles = @('\Users\', '/Users/')
    if ($env:USERNAME -and $env:USERNAME.Length -ge 4) { $needles += $env:USERNAME }
    if ($env:USERPROFILE) { $needles += $env:USERPROFILE }
    foreach ($file in (Get-ChildItem -LiteralPath $package -Recurse -File)) {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        foreach ($needle in $needles) {
            if (Test-ContainsText $bytes $needle) { $traces.Add("$($file.Name) contains '$needle'") }
        }
    }
    Add-Result 'no personal paths or names' ($traces.Count -eq 0) $(if ($traces.Count -eq 0) { 'no user name and no profile folder in any file' } else { $traces -join '; ' })

    # --- the program checks itself, from the unpacked folder --------------------------------------------------------------
    $exe = Join-Path $package $exeName
    $before = @{}
    foreach ($root in @($env:APPDATA, $env:LOCALAPPDATA)) {
        $before[$root] = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
    }
    $filesBefore = @(Get-ChildItem -LiteralPath $package -Recurse -File | ForEach-Object { $_.FullName })

    $selfResult = Join-Path $work 'self-test.json'
    $process = Start-Process -FilePath $exe -ArgumentList @('--self-test', ('"' + $selfResult + '"')) -WorkingDirectory $elsewhere -Wait -PassThru
    $selfOk = $false
    $selfDetail = "exit code $($process.ExitCode), no result file"
    if (Test-Path -LiteralPath $selfResult) {
        $self = Get-Content -LiteralPath $selfResult -Raw | ConvertFrom-Json
        $failed = @($self.checks | Where-Object { -not $_.ok } | ForEach-Object { "$($_.name): $($_.detail)" })
        $selfOk = ($process.ExitCode -eq 0) -and $self.ok -and ($self.version -eq $version)
        $selfDetail = if ($selfOk) { "$(@($self.checks).Count) checks passed (" + (($self.checks | ForEach-Object { $_.name }) -join ', ') + ')' } else { "exit code $($process.ExitCode), version $($self.version); failed: " + ($failed -join '; ') }
        $tls = $self.checks | Where-Object { $_.name -eq 'tls' }
        if ($tls) { Add-Result 'TLS for HTTPS' ([bool]$tls.ok) $tls.detail }
    }
    Add-Result 'self-test from the unpacked folder' $selfOk $selfDetail

    # Nothing may be written anywhere else: not next to the working folder, not in the user's profile.
    $stray = New-Object System.Collections.Generic.List[string]
    if (@(Get-ChildItem -LiteralPath $elsewhere -Force).Count -gt 0) { $stray.Add('files in the working folder') }
    foreach ($root in @($env:APPDATA, $env:LOCALAPPDATA)) {
        $now = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | ForEach-Object { $_.Name })
        $new = @($now | Where-Object { $before[$root] -notcontains $_ })
        if ($new.Count -gt 0) { $stray.Add("new in $root`: " + ($new -join ', ')) }
    }
    $allowed = @('cache', 'downloads', 'launcher', 'library', 'logs', 'temp')
    foreach ($file in (Get-ChildItem -LiteralPath $package -Recurse -File)) {
        if ($filesBefore -contains $file.FullName) { continue }
        $top = $file.FullName.Substring($packagePrefix.Length).Split('\')[0]
        if ($allowed -notcontains $top) { $stray.Add("new file $($file.FullName.Substring($packagePrefix.Length))") }
    }
    Add-Result 'portable: writes only into its own folders' ($stray.Count -eq 0) $(if ($stray.Count -eq 0) { 'nothing in the working folder, the user profile or beside the program' } else { $stray -join '; ' })

    # --- the launcher accepts the package as its own update -------------------------------------------------------
    if ($version) {
        $checkResult = Join-Path $work 'check-package.json'
        $checkWork = Join-Path $work 'check-work'
        New-Item -ItemType Directory -Path $checkWork -Force | Out-Null
        $q = { param($s) '"' + $s + '"' }
        $arguments = @('--check-package', '--zip', (& $q $Zip), '--sha256', $zipHash, '--version', $version, '--work', (& $q $checkWork), '--out', (& $q $checkResult))
        $process = Start-Process -FilePath $exe -ArgumentList $arguments -WorkingDirectory $elsewhere -Wait -PassThru
        if (Test-Path -LiteralPath $checkResult) {
            $check = Get-Content -LiteralPath $checkResult -Raw | ConvertFrom-Json
            Add-Result 'accepted as a self-update package' (($process.ExitCode -eq 0) -and $check.ok) $(if ($check.ok) { "hash, archive rules, manifest and $(@($check.files).Count) file hashes pass the launcher's own checks" } else { "$($check.error): $($check.detail)" })
        } else {
            Add-Result 'accepted as a self-update package' $false "exit code $($process.ExitCode), no result file"
        }
    }
}
catch {
    Add-Result 'verification' $false $_.Exception.Message
}
finally {
    if ($KeepFiles) { Write-Host "Unpacked package kept in $package" }
    else { Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue }
}

Write-Host ''
foreach ($result in $results) {
    $mark = if ($result.Ok) { 'ok  ' } else { 'FAIL' }
    Write-Host ("  {0} {1,-42} {2}" -f $mark, $result.Name, $result.Detail)
}
if ($ResultFile) {
    $rows = @($results | ForEach-Object { [pscustomobject]@{ name = $_.Name; ok = [bool]$_.Ok; detail = $_.Detail } })
    [System.IO.File]::WriteAllText($ResultFile, (ConvertTo-Json -InputObject $rows), (New-Object System.Text.UTF8Encoding($false)))
}
$failedCount = @($results | Where-Object { -not $_.Ok }).Count
Write-Host ''
if ($failedCount -eq 0) { Write-Host "All $($results.Count) checks passed: $zipName" ; exit 0 }
Write-Host "$failedCount of $($results.Count) checks FAILED: $zipName"
exit 1
