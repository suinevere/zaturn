param([string]$BaseIso,[string]$GamesDir,[string]$OutDir,[string]$Name,
      [string]$Xorriso,[string]$Iso2raw,[string]$BgDir)

# BgDir, when given and present, is mapped to /BG in the SAME xorriso commit as
# the games. Two commits would mean two full rewrites of a several-hundred-MB
# ISO, and the second would have to re-restore IP.BIN over the first's output.
# One commit with two -map arguments costs a single pass and one system area to
# protect. room_art.cxx opens /BG/<STEM>.CGL off the disc root, so the directory
# name here is the contract with the runtime.

# IP.BIN is the first 16 sectors (16 * 2048) of the ISO. xorriso rewrites the
# system area when it commits, so we hold those bytes and put them back after.
$IpBinSize = 32768

# read_head <path> -> byte[]; the first $IpBinSize bytes only, so a
# several-hundred-MB ISO never lands in memory.
function Read-Head([string]$Path) {
    $buf = New-Object byte[] $IpBinSize
    $fs = [System.IO.File]::OpenRead($Path)
    try {
        $read = 0
        while ($read -lt $IpBinSize) {
            $n = $fs.Read($buf, $read, $IpBinSize - $read)
            if ($n -le 0) { break }
            $read += $n
        }
        if ($read -lt $IpBinSize) { Write-Error "$Path is shorter than IP.BIN"; exit 1 }
    } finally { $fs.Dispose() }
    return $buf
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$inj = Join-Path $OutDir "$Name`_injected.iso"

# 1) hold IP.BIN
$ip = Read-Head $BaseIso

# 2) inject game files into /Z3 (rewrites the ISO, clobbering the system area)
# -rockridge off is REQUIRED: xorriso enables Rock Ridge by default, which adds
# SUSP/PX system-use fields to every directory record (34-46 bytes -> 96-132).
# Sega mastering never emits those, and the Saturn CD block's ISO9660 parser --
# the one the BIOS uses to find the first-read file 0.BIN -- chokes on them, so
# the patched disc stops booting. -joliet off guards the same way.
$xargs = @('-indev', $BaseIso, '-outdev', $inj, '-rockridge', 'off', '-joliet', 'off',
            '-map', $GamesDir, '/Z3')
if ($BgDir -and (Test-Path -LiteralPath $BgDir)) {
    $xargs += @('-map', $BgDir, '/BG')
    Write-Host "Also injecting room backgrounds from $BgDir -> /BG"
}
$xargs += '-commit'
& $Xorriso @xargs 2>$null
if ($LASTEXITCODE -ne 0) { Write-Error "xorriso injection failed"; exit 1 }
if (-not (Test-Path $inj) -or (Get-Item $inj).Length -le $IpBinSize) {
    Write-Error "xorriso produced no injected ISO"; exit 1
}

# 3) restore IP.BIN onto the front, in place, without truncating the rest
$fs = [System.IO.File]::OpenWrite($inj)
try { $fs.Write($ip, 0, $IpBinSize) } finally { $fs.Dispose() }

# 4) verify preservation
if (Compare-Object $ip (Read-Head $inj)) { Write-Error "IP.BIN not preserved"; exit 1 }

# 5) ISO -> MODE1/2352 raw
& $Iso2raw $inj -o "$OutDir\$Name.bin"
if ($LASTEXITCODE -ne 0) { Write-Error "iso2raw conversion failed"; exit 1 }

# 6) track-1 cue (SDK canonical form)
"FILE `"$Name.bin`" BINARY","  TRACK 01 MODE1/2352","    INDEX 01 00:00:00" | Set-Content "$OutDir\$Name.cue"
Remove-Item -Force -Path $inj
Write-Host "Injected games -> $OutDir\$Name.bin"
