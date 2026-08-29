# Converts the boot splash jingle to the raw 8-bit signed mono PCM format
# saturn/src/sound/boot_music.cxx loads whole into Low Work RAM and plays
# from memory during the Suinevere splash -- not CD-DA, so it never fights
# the splash's own CD reads for the drive. $InFile is whatever pvms.bat read out
# of CONFIG.ME (SUINEVERE_MUSIC) -- any format sox can read,
# .wav and .ogg both in use today. Uses SaturnRingLib's bundled sox
# (pvms.bat resolves $Sox to SaturnRingLib/Compiler/msys2/usr/bin/sox.exe).
# Missing sox or a missing source file is a warning, not a hard failure, so
# the rest of the compile still completes.
param(
    [Parameter(Mandatory=$true)][string]$Sox,
    [Parameter(Mandatory=$true)][string]$InFile,
    [Parameter(Mandatory=$true)][string]$OutDir,
    [Parameter(Mandatory=$true)][string]$OutName
)

if (-not (Test-Path -LiteralPath $Sox)) {
    Write-Warning "sox not found at $Sox -- skipping boot music conversion"
    exit 0
}
if (-not (Test-Path -LiteralPath $InFile)) {
    Write-Warning "Boot music source not found: $InFile -- skipping boot music conversion"
    exit 0
}

if (-not (Test-Path $OutDir)) {
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
}

$outPath = Join-Path $OutDir $OutName

# -D disables sox's automatic dither. sox adds dither on its own whenever an
# output is shallower than its input, which every conversion here is: these go
# to 8-bit. At CD depth that dither is inaudible and worth having, but 8-bit
# puts its noise floor about 48 dB down, and against a quiet game cue that is a
# plain, constant hiss over the whole track. It affected both the splash jingle
# and the loading cue equally because both are made here, by this one line,
# which is also why nothing in the playback code could ever account for it.
#
# What is traded away is dither's actual job -- without it, very low-level
# passages quantise to a stepped waveform rather than to noise. That is the
# right trade at this depth and for this material: short, mid-level music cues
# under a disc read, where the hiss was audible and the distortion is not.
& $Sox -G -D $InFile -t raw -r 22050 -e signed-integer -b 8 -c 1 $outPath
if ($LASTEXITCODE -ne 0) {
    Write-Warning "sox failed converting boot music"
    exit 1
}

Write-Host "Converted boot music -> $outPath"
