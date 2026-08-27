# Regenerate the per-game "solution" overlay for ALL games into ONE C file.
#
# Why one file, not one-per-game:
#   * The typeahead DICTIONARY + grammar is decoded at RUNTIME on the Saturn
#     (build_typeahead_from_story in typeahead_extract.c), for any v3 game. The
#     old gen_typeahead.py baked table (build_zork_typeahead) has no callers --
#     it is dead. So there is nothing to generate per game for extraction.
#   * The solution overlay (gen_solution.py) emits a single apply_solution_overlay
#     plus a SOLUTIONS[] table keyed by each story's release number + serial
#     (Z-header 0x02 / 0x12). At load, the runtime picks the row matching the
#     loaded game -- that IS the dynamic wiring. Emitting one file per game makes
#     every file redefine apply_solution_overlay/SOLUTIONS -> link collisions.
#   * Exception: the netbin links a second, Zork-I-only copy of this same file
#     (typeahead_solution_zork1.c) instead of the 25-game table, since it serves
#     one game only. Same symbols, never linked together -- see the invocation
#     at the end of this script.
#
# Run from tools/typeahead/ :  ./gen_all.ps1
# Then rebuild:               cd ../../saturn ; ./compile.bat

$ErrorActionPreference = "Stop"

# NB: $args is a reserved automatic variable in PowerShell and += inside a
# ForEach-Object block won't accumulate to the outer scope -- use a plain foreach
# and a non-reserved name.
$gameArgs = @()
foreach ($f in (Get-ChildItem -Path "../../saturn/cd/data/Z3/*.Z3" | Sort-Object Name)) {
    $name = $f.BaseName
    $win  = "./solutions/$name.WIN"
    if ((Test-Path $win) -and ((Get-Item $win).Length -gt 0)) {
        $gameArgs += "--game"
        $gameArgs += "../../saturn/cd/data/Z3/$name.Z3:$win"
    } else {
        Write-Host "skip $name (no non-empty $win)"
    }
}

Write-Host "Generating overlay for $($gameArgs.Count / 2) games -> typeahead_solution.c"
python gen_solution.py @gameArgs --out "../../saturn/src/typeahead_solution.c"

# The netbin links a Zork-I-only copy instead of the 25-game table: it serves
# one game, and the full table is 64.8 KB against this slice's 3.8 KB. Same symbol,
# never linked together -- the netbin's source list is explicit and the CD build's
# glob filters the netbin-only sources out.
python gen_solution.py `
    --game ../../saturn/cd/data/Z3/ZORK1.Z3:./solutions/ZORK1.WIN `
    --out  ../../saturn/src/input/typeahead_solution_zork1.c
