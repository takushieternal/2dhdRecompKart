; short_races.asm: every kart starts with its lap counter 3 laps in, so races are 2 laps.
; Shows the patch workflow. The lap counter lives at kart+$C1 ($7F = before the line,
; $80 = lap 1 ... $85 = finished). The kart init code builds it with ORA #$7F00 at $81F108
; and stores it with STA $C0,X at $81F10C (found with the smktrace `watch 10c1` command).
;
;   asar --fix-checksum=on mods/short_races.asm build/smk_mod.sfc
hirom

org $81F108
    ORA.w #$8200        ; was ORA.w #$7F00
