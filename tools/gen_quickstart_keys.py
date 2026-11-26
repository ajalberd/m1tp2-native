#!/usr/bin/env python3
"""
Generate a crisp, high-resolution (1920x1080) M1 Tank Platoon II Quickstart
Keyboard Layout reference image (docs/M1TP2_Quickstart_Keys.png).
Controls verified directly against M1_Tank_Platoon_II_Manual.pdf (pp. 28-31)
and M1TP2 v1.2 patch notes (M1TP2Readme.txt).
"""
import os
from PIL import Image, ImageDraw, ImageFont

W, H = 1920, 1080
BG = (13, 19, 27)
PANEL_BG = (19, 27, 38)
PANEL_BORDER = (48, 70, 92)

# Category palettes: (fill, border, accent_text)
CATS = {
    "view":   ((22, 46, 68),  (66, 165, 245), (125, 210, 255)),
    "gun":    ((64, 34, 24),  (239, 132, 68), (255, 192, 128)),
    "drive":  ((22, 56, 36),  (74, 212, 122), (138, 245, 172)),
    "sys":    ((44, 32, 64),  (168, 125, 245),(208, 180, 255)),
    "native": ((18, 66, 48),  (60, 240, 145), (110, 255, 180)),
    "dim":    ((25, 33, 44),  (58, 72, 88),   (120, 138, 156)),
}

def load_font(size, bold=False):
    candidates = [
        "C:/Windows/Fonts/segoeuib.ttf" if bold else "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/consolab.ttf" if bold else "C:/Windows/Fonts/consola.ttf",
        "C:/Windows/Fonts/arialbd.ttf"  if bold else "C:/Windows/Fonts/arial.ttf",
    ]
    for p in candidates:
        if os.path.exists(p):
            return ImageFont.truetype(p, size)
    return ImageFont.load_default()

F_TITLE  = load_font(38, bold=True)
F_SUB    = load_font(20, bold=False)
F_LEG    = load_font(18, bold=True)
F_KEY    = load_font(17, bold=True)
F_LBL    = load_font(14, bold=True)
F_SHIFT  = load_font(13, bold=False)
F_SEC    = load_font(20, bold=True)
F_BODY   = load_font(17, bold=False)
F_BODYB  = load_font(17, bold=True)

def draw_key(d, x, y, w, h, key_txt, main_lbl="", shift_lbl="", cat="dim"):
    fill, border, accent = CATS[cat]
    r = 8
    d.rounded_rectangle([x, y, x + w, y + h], radius=r, fill=fill, outline=border, width=2)
    # Key cap top-left
    d.text((x + 8, y + 6), key_txt, font=F_KEY, fill=(240, 246, 252) if cat != "dim" else (150, 165, 180))
    # Main label
    if main_lbl:
        lines = main_lbl.split("\n")
        ly = y + 30
        for ln in lines:
            d.text((x + 8, ly), ln, font=F_LBL, fill=accent)
            ly += 16
    # Shift label near bottom
    if shift_lbl:
        d.text((x + 8, y + h - 20), shift_lbl, font=F_SHIFT, fill=(228, 212, 132))

def build_image(out_path):
    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # Top header banner
    d.rectangle([0, 0, W, 94], fill=(17, 26, 36))
    d.line([(0, 94), (W, 94)], fill=(58, 155, 95), width=3)
    d.text((34, 16), "M1 TANK PLATOON II", font=F_TITLE, fill=(85, 242, 145))
    d.text((438, 16), "QUICKSTART KEYBOARD & GUNNERY REFERENCE", font=F_TITLE, fill=(235, 242, 248))
    d.text((36, 62), "Verified against Official Manual (pp. 28-31) + v1.2 Patch Notes + Native Recomp Hotkeys  |  Yellow = Shift + Key",
           font=F_SUB, fill=(158, 178, 198))

    # Category legend pills
    legend = [
        ("VIEWS & STATIONS", "view"),
        ("GUNNERY & AMMO",   "gun"),
        ("DRIVING & PLATOON","drive"),
        ("SMOKE / MAP / SYS","sys"),
        ("NATIVE RECOMP",    "native"),
    ]
    lx = 36
    ly = 110
    for text, cat in legend:
        fill, border, accent = CATS[cat]
        pw = int(d.textlength(text, font=F_LEG)) + 34
        d.rounded_rectangle([lx, ly, lx + pw, ly + 32], radius=6, fill=fill, outline=border, width=2)
        d.ellipse([lx + 10, ly + 10, lx + 22, ly + 22], fill=border)
        d.text((lx + 28, ly + 5), text, font=F_LEG, fill=accent)
        lx += pw + 14

    # Main keyboard frame
    kb_x, kb_y = 34, 156
    kb_w, kb_h = W - 68, 608
    d.rounded_rectangle([kb_x, kb_y, kb_x + kb_w, kb_y + kb_h], radius=12, fill=PANEL_BG, outline=PANEL_BORDER, width=2)

    u = 94   # base key unit width
    gap = 8
    kh = 84  # key height
    sx = kb_x + 22

    # --- ROW 0: Function Keys ---
    ry = kb_y + 18
    f_row = [
        ("ESC", "Menu /\nBack",   "",         "sys",    0.95),
        (None,  "",               "",         "dim",    0.35),
        ("F1",  "TC\nCockpit",    "S:Tank #1","view",   1.0),
        ("F2",  "Gunner\nCockpit","S:Tank #2","view",   1.0),
        ("F3",  "IVIS Map\nPlan", "S:Tank #3","view",   1.0),
        ("F4",  "GPS\nSight",     "S:Tank #4","view",   1.0),
        (None,  "",               "",         "dim",    0.22),
        ("F5",  "Aux\nSight",     "",         "view",   1.0),
        ("F6",  "TC Vision\nBlks","",         "view",   1.0),
        ("F7",  "TC Hatch\n.50 Cal","",       "view",   1.0),
        ("F8",  "Chase\nView",    "",         "view",   1.0),
        (None,  "",               "",         "dim",    0.22),
        ("F9",  "Prev\nVehicle",  "S:Prev Plt","view",  1.0),
        ("F10", "Next\nVehicle",  "S:Next Plt","view",  1.0),
        ("F11", "Toggle\n2D Filt","",         "native", 1.0),
        ("F12", "Recomp\nOverlay","",         "native", 1.0),
    ]
    cx = sx
    for k, m, sh, c, mult in f_row:
        kw = int(u * mult)
        if k is not None:
            draw_key(d, cx, ry, kw, kh, k, m, sh, c)
        cx += kw + gap

    # --- ROW 1: Number Row ---
    ry += kh + 14
    num_row = [
        ("`",   "",            "",          "dim", 0.90),
        ("1",   "Load\nSABOT", "S:Man Rng", "gun", 1.0),
        ("2",   "Load\nHEAT",  "S:Man Rng", "gun", 1.0),
        ("3",   "Load\nMPAT",  "S:Man Rng", "gun", 1.0),
        ("4",   "Load\nSTAFF", "S:Man Rng", "gun", 1.0),
        ("5",   "Select\nCOAX","S:Man Rng", "gun", 1.0),
        ("6",   "Battle\nSight","S:Man Rng","gun", 1.0),
        ("7",   "",            "S:Man Rng", "dim", 1.0),
        ("8",   "Air /\nGround","S:Man Rng","gun", 1.0),
        ("9",   "",            "S:Man Rng", "dim", 1.0),
        ("0",   "",            "S:Man Rng", "dim", 1.0),
        ("-",   "Reticle\nDim","",          "gun", 1.0),
        ("=",   "Reticle\nBright","",       "gun", 1.0),
        ("BKSP","Smoke\nGrenade","S:Arm SG","sys", 1.80),
    ]
    cx = sx
    for k, m, sh, c, mult in num_row:
        kw = int(u * mult)
        draw_key(d, cx, ry, kw, kh, k, m, sh, c)
        cx += kw + gap

    # --- ROW 2: QWERTY ---
    ry += kh + 10
    q_row = [
        ("TAB", "Chase\nLetterbox","",       "sys",   1.35),
        ("Q",   "",            "S:Quit",     "sys",   1.0),
        ("W",   "Thermal\nW/B Hot","S:Wedge","gun",   1.0),
        ("E",   "Manual\nRange","",          "gun",   1.0),
        ("R",   "",            "",           "dim",   1.0),
        ("T",   "Thermal /\nNVG {T}","",     "gun",   1.0),
        ("Y",   "",            "",           "dim",   1.0),
        ("U",   "",            "",           "dim",   1.0),
        ("I",   "Map\nIcons",  "",           "sys",   1.0),
        ("O",   "",            "",           "dim",   1.0),
        ("P",   "",            "S:Pause",    "sys",   1.0),
        ("[",   "CITV\nScan",  "",           "gun",   1.0),
        ("]",   "CITV\nSearch","",           "gun",   1.0),
        ("\\",  "Echelon\nRight","",         "drive", 1.35),
    ]
    cx = sx
    for k, m, sh, c, mult in q_row:
        kw = int(u * mult)
        draw_key(d, cx, ry, kw, kh, k, m, sh, c)
        cx += kw + gap

    # --- ROW 3: ASDF ---
    ry += kh + 10
    a_row = [
        ("CAPS","",            "",            "dim",   1.60),
        ("A",   "Auto-Drive\nTank","S:Platoon","drive",1.0),
        ("S",   "Stop\nTank",  "S:Platoon",   "drive", 1.0),
        ("D",   "Damage\nDisplay","",         "sys",   1.0),
        ("F",   "Fire Will\nPlt","S:All",     "drive", 1.0),
        ("G",   "Gun to\nHull","S:Hull->Gun", "drive", 1.0),
        ("H",   "Hold Fire\nPlt","S:All",     "drive", 1.0),
        ("J",   "",            "",            "dim",   1.0),
        ("K",   ".50 Cal\nStab","",           "gun",   1.0),
        ("L",   "Map\nContours","S:Line Fm",  "sys",   1.0),
        (";",   "",            "",            "dim",   1.0),
        ("'",   "CITV /\nGPS {T}","",         "gun",   1.0),
        ("ENTER","Fire Gun /\nCITV Desig","Alt:Fullscr","gun", 2.18),
    ]
    cx = sx
    for k, m, sh, c, mult in a_row:
        kw = int(u * mult)
        draw_key(d, cx, ry, kw, kh, k, m, sh, c)
        cx += kw + gap

    # --- ROW 4: ZXCV ---
    ry += kh + 10
    z_row = [
        ("SHIFT","Modifier\n(Yellow)","",     "dim",   2.10),
        ("Z",   "10x Zoom /\nBinos","",       "gun",   1.0),
        ("X",   "3x Zoom /\nNormal","",       "gun",   1.0),
        ("C",   "1/3\nSpeed",  "S:Stag Col",  "drive", 1.0),
        ("V",   "2/3\nSpeed",  "S:Vee Form",  "drive", 1.0),
        ("B",   "Max\nSpeed",  "",            "drive", 1.0),
        ("N",   "",            "",            "dim",   1.0),
        ("M",   "",            "",            "dim",   1.0),
        (",",   "Engage\nLeft","S:Eng Fwd",   "drive", 1.0),
        (".",   "Engage\nRight","S:Eng Rear", "drive", 1.0),
        ("/",   "Smoke\nGen",  "S:Ech Left",  "sys",   1.0),
        ("SHIFT","Modifier",   "",            "dim",   1.80),
    ]
    cx = sx
    for k, m, sh, c, mult in z_row:
        kw = int(u * mult)
        draw_key(d, cx, ry, kw, kh, k, m, sh, c)
        cx += kw + gap

    # --- ROW 5: Bottom Row + Arrow Cluster ---
    ry += kh + 10
    draw_key(d, sx, ry, int(u*1.4), kh, "CTRL", "", "", "dim")
    draw_key(d, sx + int(u*1.4) + gap, ry, int(u*1.4), kh, "ALT", "Alt+Enter\nFullscreen", "", "native")
    sp_x = sx + int(u*2.8) + gap*2
    sp_w = int(u*6.5)
    draw_key(d, sp_x, ry, sp_w, kh, "SPACE", "LASER RANGE FINDER (GPS / Aux Sight)", "Lase target before firing for exact superelevation & FCS lead", "gun")
    draw_key(d, sp_x + sp_w + gap, ry, int(u*1.4), kh, "ALT", "Alt+F\nMap Sync", "", "dim")

    # Right-side Arrow Driving Cluster (aligned neatly at right of keyboard panel)
    ax = kb_x + kb_w - 320
    ay = ry - kh - 10
    draw_key(d, ax + 102, ay, 96, kh, "UP",    "Accel /\nForward", "", "drive")
    draw_key(d, ax,       ry, 96, kh, "LEFT",  "Steer\nLeft",      "", "drive")
    draw_key(d, ax + 102, ry, 96, kh, "DOWN",  "Brake /\nReverse", "", "drive")
    draw_key(d, ax + 204, ry, 96, kh, "RIGHT", "Steer\nRight",     "", "drive")

    # Right-side Mouse Control box aligned with F-row down to above UP arrow
    mx = kb_x + kb_w - 320
    my = kb_y + 18
    mw = 300
    mh = (ay - 12) - my
    d.rounded_rectangle([mx, my, mx + mw, my + mh], radius=10, fill=(22, 34, 48), outline=(78, 175, 128), width=2)
    d.text((mx + 14, my + 12), "MOUSE & GUNNERY", font=F_SEC, fill=(100, 245, 155))
    d.line([(mx + 14, my + 40), (mx + mw - 14, my + 40)], fill=(50, 85, 70), width=1)
    mouse_lines = [
        ("Left-Click in Sight:", "Take gun control / Fire"),
        ("Right-Click:",         "Release gun / Lase (in sight)"),
        ("Precision Stick:",     "Deadzone-free smooth slew"),
        ("Direct Raw Mouse:",    "1:1 aim (switch in F12)"),
        ("Smooth FCS Lead:",     "0.45s avg fixes 90-deg"),
        ("",                     "broadside forward overshoot"),
        ("IVIS Map (F3):",       "Click units & set waypoints"),
    ]
    mly = my + 52
    for k, v in mouse_lines:
        if k:
            d.text((mx + 14, mly), k, font=F_BODYB, fill=(235, 225, 150))
            mly += 21
        d.text((mx + 22, mly), v, font=F_SHIFT, fill=(205, 220, 232))
        mly += 22

    # --- Bottom 3-Column Quickstart Combat Cards ---
    by = kb_y + kb_h + 18
    bh = H - by - 22
    cw = (W - 68 - 32) // 3
    cards = [
        ("1. QUICK COMBAT LOOP (GPS GUNNERY)", (239, 132, 68), [
            "Press F4 for Gunner's Primary Sight (GPS).",
            "Press T for Thermal Imaging, Z for 10x Zoom (X = 3x).",
            "Left-Click inside the sight to take gun control.",
            "Aim on target, press SPACE to Lase Range, then Fire (LMB/Enter).",
            "Press 1=SABOT (Tanks), 2=HEAT, 3=MPAT (Helos/IFV), 4=STAFF.",
        ]),
        ("2. PLATOON COMMAND & HUNTER-KILLER", (66, 165, 245), [
            "Press F3 for IVIS Battleplanner Map (L=Contours, I=Icons).",
            "Use Shift+F1..F4 to jump directly into Tank #1-#4.",
            "Press F1 (TC Cockpit) -> ] CITV Search -> LMB Designates target.",
            "Press F7 (TC Outhatch) for .50 Cal MG (Z=Binos, T=NVG, K=Stab).",
            "Press Shift+G to align Hull armor toward your Gun direction.",
        ]),
        ("3. NATIVE RECOMP HOTKEYS & TIPS", (60, 240, 145), [
            "Press F12 anytime for Settings, Gunnery/FCS & Quick Keys.",
            "Press F11 to toggle 2D Menu Bilinear/Nearest filter.",
            "Press Alt+Enter to toggle Borderless Fullscreen / Windowed.",
            "Campaign Map shows live Win/Loss & Front Intel on right bar.",
            "Drop mods into mods/<name>/ for unlimited VFS asset overrides.",
        ]),
    ]
    for idx, (title, col, lines) in enumerate(cards):
        cx0 = 34 + idx * (cw + 16)
        d.rounded_rectangle([cx0, by, cx0 + cw, by + bh], radius=10, fill=PANEL_BG, outline=col, width=2)
        d.text((cx0 + 16, by + 14), title, font=F_SEC, fill=col)
        d.line([(cx0 + 16, by + 44), (cx0 + cw - 16, by + 44)], fill=PANEL_BORDER, width=1)
        ty = by + 56
        for ln in lines:
            d.text((cx0 + 16, ty), "- " + ln, font=F_BODY, fill=(212, 224, 236))
            ty += 36

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path, "PNG", optimize=True)
    print(f"Generated {out_path} ({W}x{H}, {os.path.getsize(out_path)} bytes)")

if __name__ == "__main__":
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    build_image(os.path.join(root, "docs", "M1TP2_Quickstart_Keys.png"))
