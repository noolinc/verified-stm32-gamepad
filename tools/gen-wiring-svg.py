#!/usr/bin/env python3
"""Draw docs/images/wiring.svg. The pin map below mirrors firmware/stm32f411/board.c; keep them in sync.

Header order is the WeAct Black Pill (v2.0/v3.0) top view. Re-run after changing pins:
    python3 tools/gen-wiring-svg.py docs/images/wiring.svg
"""
import sys
left  = ["VB","C13","C14","C15","R","A0","A1","A2","A3","A4","A5","A6","A7","B0","B1","B2","B10","3V3","G","5V"]
right = ["3V3","G","5V","B9","B8","B7","B6","B5","B4","B3","A15","A12","A11","A10","A9","A8","B15","B14","B13","B12"]
btn = {"B0":1,"B1":2,"B3":3,"B4":4,"B5":5,"B6":6,"B7":7,"B8":8,"B9":9,"B10":10,"B12":11,"B13":12,"B14":13,"B15":14,"A8":15,"A15":16}
fn = {f"{k}":(f"Button {v}","btn") for k,v in btn.items()}
fn.update({"A9":("D-pad up","dpad"),"A10":("D-pad down","dpad"),"C14":("D-pad left","dpad"),"C15":("D-pad right","dpad"),
  "A1":("Left stick X","axis"),"A2":("Left stick Y","axis"),"A3":("Right stick X (Z)","axis"),"A4":("Right stick Y (Rz)","axis"),
  "C13":("On-board LED","led"),"A11":("USB D− (on board)","usb"),"A12":("USB D+ (on board)","usb"),
  "3V3":("3.3 V","pwr"),"G":("GND","gnd"),"5V":("5 V","pwr")})
col = {"btn":"#2563eb","dpad":"#7c3aed","axis":"#059669","led":"#d97706","usb":"#6b7280","pwr":"#dc2626","gnd":"#111827"}
W,H = 980, 880; bx,by,bw = 390, 130, 200; pitch = 28; top = by+70
o = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}" font-family="Helvetica,Arial,sans-serif">',
 f'<rect width="{W}" height="{H}" fill="#ffffff"/>',
 '<text x="490" y="40" font-size="22" font-weight="bold" text-anchor="middle" fill="#111827">STM32F411 Black Pill wiring (top view, USB-C up)</text>',
 '<text x="490" y="68" font-size="14" text-anchor="middle" fill="#4b5563">Each switch connects its pin to GND (internal pull-ups, active-low).</text>',
 '<text x="490" y="88" font-size="14" text-anchor="middle" fill="#4b5563">Each stick is two 10 kΩ pots: ends to 3V3 and GND, wiper to the pin.</text>',
 f'<rect x="{bx}" y="{by}" width="{bw}" height="{20*pitch+90}" rx="10" fill="#1f2937"/>',
 f'<rect x="{bx+70}" y="{by-14}" width="60" height="34" rx="6" fill="#9ca3af"/>',
 f'<text x="{bx+100}" y="{by+8}" font-size="12" text-anchor="middle" fill="#111827">USB</text>',
 f'<rect x="{bx+55}" y="{by+250}" width="90" height="90" fill="#374151" stroke="#4b5563"/>',
 f'<text x="{bx+100}" y="{by+290}" font-size="12" text-anchor="middle" fill="#e5e7eb">STM32</text>',
 f'<text x="{bx+100}" y="{by+306}" font-size="12" text-anchor="middle" fill="#e5e7eb">F411CEU6</text>']
for side, pins in (("L",left),("R",right)):
    for i,p in enumerate(pins):
        y = top + i*pitch
        px = bx+18 if side=="L" else bx+bw-18
        f = fn.get(p); c = col[f[1]] if f else "#9ca3af"
        o.append(f'<circle cx="{px}" cy="{y}" r="7" fill="{c if f else "#4b5563"}" stroke="#d1d5db"/>')
        tx = px+14 if side=="L" else px-14
        o.append(f'<text x="{tx}" y="{y+4}" font-size="12" fill="#f9fafb" text-anchor="{"start" if side=="L" else "end"}">{p}</text>')
        if f and f[1] not in ("pwr","gnd"):
            x2 = bx-40 if side=="L" else bx+bw+40
            o.append(f'<line x1="{px}" y1="{y}" x2="{x2}" y2="{y}" stroke="{c}" stroke-width="2"/>')
            lx = x2-8 if side=="L" else x2+8
            o.append(f'<text x="{lx}" y="{y+5}" font-size="14" fill="{c}" text-anchor="{"end" if side=="L" else "start"}" font-weight="600">{f[0]}</text>')
# legend
ly = H-30; lx = 40
for name,key in (("Buttons","btn"),("D-pad","dpad"),("Stick (ADC)","axis"),("LED","led"),("USB","usb"),("Power","pwr"),("Ground","gnd")):
    o.append(f'<circle cx="{lx}" cy="{ly}" r="7" fill="{col[key]}"/><text x="{lx+14}" y="{ly+5}" font-size="14" fill="#111827">{name}</text>')
    lx += 128
o.append('</svg>')
open(sys.argv[1],"w").write("\n".join(o))
