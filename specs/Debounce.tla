---------------------------- MODULE Debounce ----------------------------
(***************************************************************************)
(* The per-input saturating integrator in core/gp_input.c (debounce()).   *)
(* Each tick the raw contact reads pressed or released; count moves one   *)
(* step toward it, saturating at 0 and Threshold. The output is set when  *)
(* count reaches Threshold and cleared when it reaches 0.                 *)
(*                                                                         *)
(* Window is the requirement (GP_DEBOUNCE_MS); Threshold is what the code *)
(* implements. Debounce.cfg checks Threshold = Window; DebounceBug.cfg     *)
(* seeds Threshold = 1 and must fail.                                      *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS Window, Threshold

ASSUME Window \in Nat \ {0} /\ Threshold \in Nat \ {0}

VARIABLES raw,      \* this tick's contact reading
          count,    \* integrator
          out,      \* debounced output
          run,      \* consecutive ticks raw has held its value (capped)
          pressed   \* pressed ticks since power-up (capped), for glitch check

vars == <<raw, count, out, run, pressed>>

Cap(n) == IF n > Window + 1 THEN Window + 1 ELSE n

Init == /\ raw = FALSE
        /\ count = 0
        /\ out = FALSE
        /\ run = 0
        /\ pressed = 0

Step(r) ==
    LET c == IF r THEN (IF count < Threshold THEN count + 1 ELSE count)
                  ELSE (IF count > 0 THEN count - 1 ELSE count)
    IN /\ raw' = r
       /\ count' = c
       /\ out' = IF c >= Threshold THEN TRUE ELSE IF c = 0 THEN FALSE ELSE out
       /\ run' = IF r = raw THEN Cap(run + 1) ELSE 1
       /\ pressed' = IF r THEN Cap(pressed + 1) ELSE pressed

Next == \E r \in BOOLEAN : Step(r)

Spec == Init /\ [][Next]_vars

TypeOK == /\ raw \in BOOLEAN /\ out \in BOOLEAN
          /\ count \in 0..Threshold
          /\ run \in 0..(Window + 1) /\ pressed \in 0..(Window + 1)

(* A contact held for Window ticks is reported. *)
StableIsReported == (run >= Window) => (out = raw)

(* Fewer than Window pressed ticks since power-up never register a press:
   bounce shorter than the window is ignored. *)
ShortGlitchIgnored == (pressed < Window) => (out = FALSE)

(* The output only ever moves toward the current reading. *)
NoSpuriousToggle == [][(out' # out) => (out' = raw')]_vars
=============================================================================
