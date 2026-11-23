----------------------------- MODULE UsbHid -----------------------------
(***************************************************************************)
(* Device side of the gamepad's USB link: core/usb_ctrl.c (device state, *)
(* SET_ADDRESS, SET_CONFIGURATION, bus reset), the EP1 IN interrupt       *)
(* endpoint, and the report scheduler gp_tx_step() in core/gp_report.c    *)
(* with idle rate 0 (send on change only). The host enumerates, may reset *)
(* or deconfigure, and polls EP1. Inputs change a bounded number of times *)
(* so every behaviour eventually goes quiet and liveness is checkable.    *)
(***************************************************************************)
EXTENDS Naturals

CONSTANTS Values,      \* possible input states (abstracted report payloads)
          MaxChanges,  \* bound on input changes
          MaxResets,   \* bound on bus resets / deconfigurations
          ReinitScheduler \* firmware calls gp_tx_init() on reset/configure

None == "none"


VARIABLES dev,       \* "Default" | "Address" | "Configured"
          addr,      \* assigned address (0 or 1 abstracted)
          cur,       \* current input state on the device
          lastSent,  \* gp_tx_t.last: last report queued, or None after (re)init
          inflight,  \* report sitting in the EP1 IN FIFO, or None
          hostSeen,  \* last report the host received, or None
          changes, resets

vars == <<dev, addr, cur, lastSent, inflight, hostSeen, changes, resets>>

(* The scheduler record after a reset or (re)configuration. *)
Fresh == IF ReinitScheduler THEN None ELSE lastSent

Init == /\ dev = "Default" /\ addr = 0
        /\ cur \in Values
        /\ lastSent = None /\ inflight = None /\ hostSeen = None
        /\ changes = 0 /\ resets = 0

(* ---- environment ---- *)
InputChange == /\ changes < MaxChanges
               /\ \E v \in Values \ {cur} : cur' = v
               /\ changes' = changes + 1
               /\ UNCHANGED <<dev, addr, lastSent, inflight, hostSeen, resets>>

(* Bus reset: usb_ctrl_bus_reset() + driver flushes EP1 + gp_tx_init(). *)
BusReset == /\ resets < MaxResets
            /\ dev' = "Default" /\ addr' = 0
            /\ lastSent' = Fresh /\ inflight' = None /\ hostSeen' = None
            /\ resets' = resets + 1
            /\ UNCHANGED <<cur, changes>>

(* Host deconfigures (SET_CONFIGURATION 0): EP1 closed and flushed. *)
Deconfigure == /\ dev = "Configured" /\ resets < MaxResets
               /\ dev' = "Address" /\ inflight' = None /\ lastSent' = Fresh
               /\ resets' = resets + 1
               /\ UNCHANGED <<addr, cur, hostSeen, changes>>

(* ---- host enumeration (requests handled by usb_ctrl_setup) ---- *)
SetAddress == /\ dev = "Default"
              /\ dev' = "Address" /\ addr' = 1
              /\ UNCHANGED <<cur, lastSent, inflight, hostSeen, changes, resets>>

(* SET_CONFIGURATION 1 is stalled in the Default state; accepted otherwise.
   Configuring opens EP1 and re-inits the scheduler so a fresh report goes out. *)
SetConfig == /\ dev = "Address"
             /\ dev' = "Configured" /\ lastSent' = Fresh
             /\ UNCHANGED <<addr, cur, inflight, hostSeen, changes, resets>>

HostEnum == SetAddress \/ SetConfig

(* Host polls EP1 every bInterval and takes the queued report. *)
Poll == /\ inflight # None
        /\ hostSeen' = inflight /\ inflight' = None
        /\ UNCHANGED <<dev, addr, cur, lastSent, changes, resets>>

(* ---- device superloop: one gp_tx_step() with ep_ready = EP1 empty ---- *)
Tick == /\ dev = "Configured" /\ inflight = None
        /\ lastSent # cur
        /\ inflight' = cur /\ lastSent' = cur
        /\ UNCHANGED <<dev, addr, cur, hostSeen, changes, resets>>

Next == InputChange \/ BusReset \/ Deconfigure \/ HostEnum \/ Poll \/ Tick

Spec == Init /\ [][Next]_vars
        /\ WF_vars(HostEnum) /\ WF_vars(Tick) /\ WF_vars(Poll)

(* ---- properties ---- *)
TypeOK == /\ dev \in {"Default", "Address", "Configured"}
          /\ addr \in {0, 1}
          /\ cur \in Values
          /\ lastSent \in Values \cup {None}
          /\ inflight \in Values \cup {None}
          /\ hostSeen \in Values \cup {None}

ConfiguredHasAddress == dev = "Configured" => addr # 0
DefaultHasNoAddress  == dev = "Default" => addr = 0

(* EP1 only holds a report while configured; at most one is in flight
   (inflight is a single slot, and Tick requires it empty). *)
ReportsOnlyWhenConfigured == inflight # None => dev = "Configured"

(* What is queued is what the scheduler last recorded. *)
InflightIsLast == inflight # None => inflight = lastSent

(* No change is lost: whenever the device may send, the scheduler's
   record catches up with the input in that step. *)
NoLostChange == [][(Tick => lastSent' = cur)]_vars

(* Once input and bus go quiet, the host ends up configured and holding
   exactly the current input state. *)
EventuallyDelivered == <>[](dev = "Configured" /\ hostSeen = cur)
=============================================================================
