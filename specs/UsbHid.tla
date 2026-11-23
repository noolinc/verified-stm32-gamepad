----------------------------- MODULE UsbHid -----------------------------
(***************************************************************************)
(* Device side of the gamepad's USB link: core/usb_ctrl.c (device state, *)
(* SET_ADDRESS, SET_CONFIGURATION, bus reset), the EP1 IN interrupt       *)
(* endpoint and the report scheduler gp_tx_step() in core/gp_report.c     *)
(* with idle rate 0. The host enumerates, may reset or deconfigure, and   *)
(* polls EP1 only while an application has the device open.               *)
(*                                                                         *)
(* Host behaviour found by the Docker USB/IP simulation (Linux usbhid):    *)
(* for ~50 ms after an open, reports that arrive are DISCARDED             *)
(* (HID_RESUME_RUNNING), and applications may open and close the device   *)
(* at any time. The firmware therefore never stays silent: besides        *)
(* sending on change it re-sends the current state every 50 ms             *)
(* (Keepalive). With Keepalive = FALSE (pure SET_IDLE 0 behaviour) TLC     *)
(* finds the lost-held-state behaviour (UsbHidNoKeepalive.cfg).            *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS Values,          \* abstracted report payloads
          MaxChanges,      \* bound on input changes
          MaxResets,       \* bound on bus resets / deconfigurations
          MaxCloses,       \* bound on applications closing the device
          ReinitScheduler, \* firmware calls gp_tx_init() on reset/configure
          Keepalive        \* firmware re-sends unchanged state periodically

None == "none"

VARIABLES dev, addr, cur,
          lastSent,        \* gp_tx_t.last, None after (re)init
          inflight,        \* report in the EP1 FIFO, or None
          hostSeen,        \* last report the host's HID layer accepted
          polling,         \* host has the interrupt pipe open
          dropping,        \* host is inside its post-open discard window
          changes, resets, closes

vars == <<dev, addr, cur, lastSent, inflight, hostSeen, polling, dropping,
          changes, resets, closes>>

Fresh == IF ReinitScheduler THEN None ELSE lastSent

Init == /\ dev = "Default" /\ addr = 0 /\ cur \in Values
        /\ lastSent = None /\ inflight = None
        /\ hostSeen = None /\ polling = FALSE /\ dropping = FALSE
        /\ changes = 0 /\ resets = 0 /\ closes = 0

(* ---- environment ---- *)
InputChange == /\ changes < MaxChanges
               /\ \E v \in Values \ {cur} : cur' = v
               /\ changes' = changes + 1
               /\ UNCHANGED <<dev, addr, lastSent, inflight, hostSeen,
                              polling, dropping, resets, closes>>

(* Bus reset: usb_ctrl_bus_reset(), EP1 flushed, gp_tx_init(); the host
   re-enumerates, so its pipes are closed and its state is gone. *)
BusReset == /\ resets < MaxResets
            /\ dev' = "Default" /\ addr' = 0
            /\ lastSent' = Fresh /\ inflight' = None
            /\ hostSeen' = None /\ polling' = FALSE /\ dropping' = FALSE
            /\ resets' = resets + 1
            /\ UNCHANGED <<cur, changes, closes>>

Deconfigure == /\ dev = "Configured" /\ resets < MaxResets
               /\ dev' = "Address" /\ inflight' = None
               /\ lastSent' = Fresh /\ polling' = FALSE /\ dropping' = FALSE
               /\ resets' = resets + 1
               /\ UNCHANGED <<addr, cur, hostSeen, changes, closes>>

(* ---- host enumeration ---- *)
SetAddress == /\ dev = "Default"
              /\ dev' = "Address" /\ addr' = 1
              /\ UNCHANGED <<cur, lastSent, inflight, hostSeen,
                             polling, dropping, changes, resets, closes>>

SetConfig == /\ dev = "Address"
             /\ dev' = "Configured" /\ lastSent' = Fresh
             /\ UNCHANGED <<addr, cur, inflight, hostSeen,
                            polling, dropping, changes, resets, closes>>

HostEnum == SetAddress \/ SetConfig

(* ---- application opens / closes the device ---- *)
Open == /\ dev = "Configured" /\ ~polling
        /\ polling' = TRUE /\ dropping' = TRUE
        /\ UNCHANGED <<dev, addr, cur, lastSent, inflight, hostSeen, changes, resets, closes>>

Close == /\ polling /\ closes < MaxCloses
         /\ polling' = FALSE /\ dropping' = FALSE /\ closes' = closes + 1
         /\ UNCHANGED <<dev, addr, cur, lastSent, inflight, hostSeen, changes, resets>>

EndDrop == /\ dropping /\ dropping' = FALSE
           /\ UNCHANGED <<dev, addr, cur, lastSent, inflight, hostSeen,
                          polling, changes, resets, closes>>

(* Host takes the queued report; it is discarded inside the drop window. *)
Poll == /\ polling /\ inflight # None
        /\ hostSeen' = IF dropping THEN hostSeen ELSE inflight
        /\ inflight' = None
        /\ UNCHANGED <<dev, addr, cur, lastSent, polling, dropping, changes, resets, closes>>

(* ---- device superloop: one gp_tx_step() per ms ----
   Sends on change, and (Keepalive) re-sends the unchanged state when the
   keepalive period expires; timing is abstracted to "eventually". *)
Tick == /\ dev = "Configured" /\ inflight = None
        /\ (lastSent # cur \/ Keepalive)
        /\ inflight' = cur /\ lastSent' = cur
        /\ UNCHANGED <<dev, addr, cur, hostSeen, polling, dropping, changes, resets, closes>>

Next == \/ InputChange \/ BusReset \/ Deconfigure \/ HostEnum
        \/ Open \/ Close \/ EndDrop \/ Poll \/ Tick

Spec == Init /\ [][Next]_vars
        /\ WF_vars(HostEnum) /\ WF_vars(Open) /\ WF_vars(EndDrop)
        /\ WF_vars(Poll) /\ WF_vars(Tick)

(* ---- properties ---- *)
TypeOK == /\ dev \in {"Default", "Address", "Configured"}
          /\ addr \in {0, 1} /\ cur \in Values
          /\ lastSent \in Values \cup {None} /\ inflight \in Values \cup {None}
          /\ hostSeen \in Values \cup {None}
          /\ polling \in BOOLEAN /\ dropping \in BOOLEAN

ConfiguredHasAddress == dev = "Configured" => addr # 0
DefaultHasNoAddress  == dev = "Default" => addr = 0
ReportsOnlyWhenConfigured == inflight # None => dev = "Configured"
InflightIsLast == inflight # None => inflight = lastSent
DropOnlyWhilePolling == dropping => polling

(* No change is lost: whenever the device may send, its record catches up. *)
NoLostChange == [][(Tick => lastSent' = cur)]_vars

(* Once input, bus and the application go quiet (device open), the host
   ends up holding exactly the current input state. *)
EventuallyDelivered == <>[](dev = "Configured" /\ hostSeen = cur)
=============================================================================
