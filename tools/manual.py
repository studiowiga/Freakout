# FREAKOUT (Studio Wiga) - notice PDF bilingue (anglais puis francais), illustree avec le rendu reel du plugin.
# Usage : python3 tools/manual.py <dossier des captures (tools/render)> <fichier PDF de sortie>
import os, sys, json
import numpy as np
from PIL import Image, ImageDraw, ImageFont
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib import colors
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.enums import TA_CENTER
from reportlab.platypus import (BaseDocTemplate, PageTemplate, NextPageTemplate, Frame, Paragraph, Spacer, Image as RLImage,
                                Table, TableStyle, PageBreak, CondPageBreak, KeepTogether)
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SHOTS = sys.argv[1]
OUT = sys.argv[2]
VERSION = "1.0.1"
TMP = os.path.dirname(os.path.abspath(OUT))
FONTS = os.path.join(ROOT, "assets", "fonts")

for n, f in (("Pop", "Poppins-Regular"), ("PopL", "Poppins-Light"), ("PopM", "Poppins-Medium"), ("PopB", "Poppins-Bold"),
             ("Bebas", "BebasNeue-Regular")):
    pdfmetrics.registerFont(TTFont(n, os.path.join(FONTS, f + ".ttf")))

ACC = colors.HexColor("#D6301C"); ACC_L = colors.HexColor("#FF6A4D")       # rouge des LED du visuel
DARK = colors.HexColor("#1B1C20"); GREY = colors.HexColor("#686A72"); LIGHT = colors.HexColor("#EEEEEC")

LAY = json.load(open(os.path.join(ROOT, "tools", "layout.json")))

def shot(name): return Image.open(os.path.join(SHOTS, name)).convert("RGB")
K = shot("freakout_status.png").height / 958.0          # px par unite 760 x 958

# ---------- capture annotee ----------
src = shot("freakout_status.png")
ann = src.copy(); dr = ImageDraw.Draw(ann)
fnt = ImageFont.truetype(os.path.join(FONTS, "Poppins-Bold.ttf"), 30)
led_mid = LAY["leds"][4]
marks = [  # numero, x, y (unites), decalage de la pastille
    (1, led_mid[0] - 50, led_mid[1]), (2, LAY["toggles"]["momentary"][0] + 52, LAY["toggles"]["momentary"][1] - 10),
    (3, LAY["toggles"]["dry"][0] + 52, LAY["toggles"]["dry"][1] - 10),
    (4, LAY["cap"][0][0] - 5, LAY["cap"][0][1] - 5), (5, LAY["skirt"][0][0] + 66, LAY["skirt"][0][1] - 50),
    (6, LAY["type"][0][0] - 70, LAY["type"][0][1] - 50), (7, 380, 470), (8, 380, 548),
    (9, LAY["fs"][0] + 75, LAY["fs"][1] - 50), (10, 132, 470), (11, 628, 470)]
for n, x, y in marks:
    cx, cy, r = x * K, y * K, 24
    dr.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(255, 255, 255), outline=(214, 48, 28), width=5)
    dr.text((cx, cy + 1), str(n), font=fnt, fill=(27, 28, 32), anchor="mm")
annotated = os.path.join(TMP, "freakout_annotated.png"); ann.save(annotated)

def crop_u(img_, box):
    k = img_.height / 958.0                       # chaque capture a sa propre taille
    return img_.crop(tuple(int(v * k) for v in box)).resize((int((box[2] - box[0]) * 2), int((box[3] - box[1]) * 2)), Image.LANCZOS)
def strip(images, path, gap=16):
    h = max(i.height for i in images)
    s = Image.new("RGB", (sum(i.width for i in images) + gap * (len(images) - 1), h), (255, 255, 255))
    x = 0
    for i in images: s.paste(i, (x, 0)); x += i.width + gap
    s.save(path); return path

# echelle de LED : effet actif (LED centrale), montee du larsen, pleine intensite
lb = (186, 70, 270, 440)
leds_img = strip([crop_u(shot(p), lb) for p in ("freakout_default.png", "freakout_bypass.png", "freakout_bloom.png", "freakout_full.png")],
                 os.path.join(TMP, "freakout_leds.png"))
# inverseurs (OFF / ON) et footswitch (relache / enfonce)
tb = (285, 120, 380, 420)
fb = (318, 685, 446, 810)
ctl_img = strip([crop_u(shot("freakout_full.png"), tb), crop_u(shot("freakout_pressed.png"), tb),
                 crop_u(shot("freakout_full.png"), fb), crop_u(shot("freakout_pressed.png"), fb)], os.path.join(TMP, "freakout_ctl.png"))
# ecran
disp_img = os.path.join(TMP, "freakout_disp.png")
strip([crop_u(shot("freakout_bloom.png"), (172, 460, 590, 548)), crop_u(shot("freakout_status.png"), (172, 460, 590, 548))], disp_img, gap=10)
# potards
knobs_img = strip([crop_u(shot("freakout_full.png"), (440, 95, 582, 270)), crop_u(shot("freakout_full.png"), (446, 300, 578, 455))],
                  os.path.join(TMP, "freakout_knobs.png"))

# ---------- couverture : pedale detouree, reflet au sol, halo rouge tres doux ----------
cov = shot("freakout_full.png")
msk = Image.open(os.path.join(ROOT, "tools", "pedal_mask.png")).convert("L").resize(cov.size, Image.LANCZOS)
cut = Image.new("RGBA", cov.size, (0, 0, 0, 0)); cut.paste(cov, (0, 0), msk)
cut = cut.crop(cut.getbbox())
cw, ch = cut.size
pad = int(cw * 0.16)
canvas = Image.new("RGBA", (cw + 2 * pad, int(ch * 1.30) + pad), (0, 0, 0, 255))
yy, xx = np.mgrid[0:canvas.height, 0:canvas.width].astype(np.float64)
gcx, gcy = canvas.width / 2, pad + ch * 0.42
glow = np.exp(-(((xx - gcx) / (cw * 0.62)) ** 2 + ((yy - gcy) / (ch * 0.48)) ** 2)) * 30
edge = np.clip(np.minimum.reduce([xx, canvas.width - 1 - xx, yy, canvas.height - 1 - yy]) / (pad * 0.9), 0, 1)
glow *= edge ** 2
halo = np.dstack([glow * 1.0, glow * 0.22, glow * 0.14, np.full_like(glow, 255)]).astype(np.uint8)
canvas = Image.alpha_composite(canvas, Image.fromarray(halo, "RGBA"))
refl = cut.transpose(Image.FLIP_TOP_BOTTOM)
ra = np.array(refl).astype(np.float64)
fade = np.clip(1 - np.arange(refl.height) / (refl.height * 0.22), 0, 1) ** 1.6 * 0.22
ra[..., 3] *= fade[:, None]
canvas.alpha_composite(Image.fromarray(ra.astype(np.uint8), "RGBA"), (pad, pad + ch + 6))
canvas.alpha_composite(cut, (pad, pad))
canvas = canvas.crop((0, 0, canvas.width, pad + int(ch * 1.20)))
cover = os.path.join(TMP, "freakout_cover_cutout.png"); canvas.convert("RGB").save(cover)

def img(path, width):
    im = Image.open(path); h = width * im.height / im.width
    return RLImage(path, width=width, height=h)

# ---------- styles ----------
st = {
    "h1": ParagraphStyle("h1", fontName="PopB", fontSize=20, leading=24, textColor=ACC, spaceAfter=8, spaceBefore=4, keepWithNext=1),
    "h2": ParagraphStyle("h2", fontName="PopM", fontSize=13, leading=17, textColor=DARK, spaceAfter=4, spaceBefore=10, keepWithNext=1),
    "p": ParagraphStyle("p", fontName="Pop", fontSize=9.6, leading=14, textColor=DARK, spaceAfter=5),
    "small": ParagraphStyle("small", fontName="Pop", fontSize=8.2, leading=11.5, textColor=GREY),
    "cell": ParagraphStyle("cell", fontName="Pop", fontSize=8.4, leading=11, textColor=DARK),
    "cellb": ParagraphStyle("cellb", fontName="PopM", fontSize=8.4, leading=11, textColor=DARK),
    "cover": ParagraphStyle("cover", fontName="Bebas", fontSize=54, leading=58, textColor=colors.white, alignment=TA_CENTER),
    "coversub": ParagraphStyle("coversub", fontName="Pop", fontSize=12, leading=16, textColor=colors.HexColor("#BDBDBA"), alignment=TA_CENTER),
    "coverlang": ParagraphStyle("coverlang", fontName="PopM", fontSize=8.5, leading=11, textColor=ACC_L),
}
def P(t, s="p"): return Paragraph(t, st[s])
def bullets(items): return [P("&bull;&nbsp;&nbsp;" + i) for i in items]
def table(rows, widths, header=True):
    data = [[P(c, "cellb" if (header and r == 0) else "cell") for c in row] for r, row in enumerate(rows)]
    t = Table(data, colWidths=widths, repeatRows=1 if header else 0)
    style = [("VALIGN", (0, 0), (-1, -1), "TOP"), ("LINEBELOW", (0, 0), (-1, -1), 0.3, colors.HexColor("#D9D9D6")),
             ("TOPPADDING", (0, 0), (-1, -1), 3), ("BOTTOMPADDING", (0, 0), (-1, -1), 3)]
    if header: style += [("BACKGROUND", (0, 0), (-1, 0), LIGHT), ("LINEBELOW", (0, 0), (-1, 0), 0.8, ACC)]
    t.setStyle(TableStyle(style)); return t

# ---------- donnees (identiques au code livre : PluginProcessor.cpp) ----------
TYPES = ["SUB", "1ST", "2ND", "3RD", "5TH", "NAT LOW", "NAT HI"]
FACTORY = [  # nom, gain, onset, type, momentary, dry
    ("Manual Setup", 5.0, 5.0, 3, 0, 1), ("Live Feedback Wail", 6.0, 4.5, 6, 0, 1), ("Studio Swell (Momentary)", 4.2, 5.8, 6, 1, 1),
    ("Unison Sustain", 5.5, 2.0, 1, 0, 1), ("Octave Scream", 7.0, 2.5, 2, 0, 1), ("Twelfth Lead", 6.0, 3.5, 3, 0, 1),
    ("Fifth Harmonic Squeal", 6.5, 3.0, 4, 0, 1), ("Sub Drone", 6.0, 6.0, 0, 0, 1), ("Minor Key Natural", 5.0, 5.0, 5, 0, 1),
    ("Doom Bloom", 7.0, 7.5, 5, 0, 1), ("EBow Swell", 6.0, 6.0, 1, 0, 0), ("Theremin Ghost", 6.5, 3.5, 3, 0, 0),
    ("Instant Squeal", 9.0, 0.5, 6, 1, 1)]
def onset_s(v):
    s = 0.03 * 200 ** (v / 10.0)
    return f"{s * 1000:.0f} ms" if s < 1 else f"{s:.1f} s"
FACT_DESC = {
 "en": ["The setup procedure of the original manual: GAIN, ONSET and TYPE at 12 o'clock, latching footswitch, dry on.",
        "NAT HI with a little more gain: the feedback picks its own harmonic and jumps around like a cranked amp.",
        "Settings from the Tape Op review (gain 11 o'clock, onset 1 o'clock, NAT HI) with the footswitch in momentary mode.",
        "Unison feedback with a fast onset: endless sustain of the played note.",
        "Octave above (2nd harmonic), fast and loud: the classic screaming lead feedback.",
        "3rd harmonic (an octave and a fifth up): singing, vocal lead feedback.",
        "5th harmonic (two octaves and a major third up): the piercing squeal.",
        "One octave below, slow bloom: a dark drone under sustained notes.",
        "NAT LOW: natural feedback that never goes to the 5th harmonic, so it never sounds major over minor chords.",
        "NAT LOW, more gain and a very slow onset: huge swells for doom and post-metal.",
        "Dry off, unison: the note only appears as it swells in, like an EBow.",
        "Dry off, 3rd harmonic: a ghostly, theremin-like line above what you play.",
        "Near full gain, instant onset, momentary: hit the footswitch for an immediate burst of feedback."],
 "fr": ["La procédure de réglage du manuel d'origine : GAIN, ONSET et TYPE à 12 h, footswitch à accrochage, DRY en marche.",
        "NAT HI avec un peu plus de gain : le larsen choisit lui-même son harmonique et saute de l'une à l'autre, comme un ampli à fond.",
        "Le réglage du test Tape Op (gain à 11 h, onset à 13 h, NAT HI) avec le footswitch en mode momentané.",
        "Larsen à l'unisson, montée rapide : sustain infini de la note jouée.",
        "Octave au-dessus (2e harmonique), rapide et fort : le larsen hurlant classique des solos.",
        "3e harmonique (une octave et une quinte au-dessus) : un larsen chantant, presque vocal.",
        "5e harmonique (deux octaves et une tierce majeure au-dessus) : le sifflement perçant.",
        "Octave en dessous, montée lente : un bourdon sombre sous les notes tenues.",
        "NAT LOW : larsen naturel qui ne monte jamais à la 5e harmonique, donc jamais « majeur » sur un accord mineur.",
        "NAT LOW, plus de gain et une montée très lente : d'immenses gonflements pour le doom et le post-metal.",
        "DRY coupé, unisson : la note n'apparaît qu'en gonflant, comme avec un EBow.",
        "DRY coupé, 3e harmonique : une ligne fantomatique façon thérémine au-dessus de ce que vous jouez.",
        "Gain presque à fond, montée instantanée, momentané : un coup de footswitch et le larsen part aussitôt."],
}
def pc(v): return f"{round(v * 10)} %"

T = {}
T["en"] = dict(
 lang="ENGLISH", sub="Natural harmonic feedback generator", ver="User manual · version " + VERSION,
 intro_t="Introduction",
 intro=["<b>FreakOut</b> is a Studio Wiga VST3 plugin that creates real-sounding guitar feedback at any volume, clean or distorted, through headphones or a quiet amp simulator. It is modelled on the behaviour of the <b>DigiTech FreqOut</b> Natural Feedback Creator: same controls, same seven feedback types, same LED ladder, same momentary and dry switches.",
        "Like the pedal, FreakOut follows the note you play, chooses which harmonic of that note will feed back, then makes it bloom: the feedback starts inaudible, grows exponentially as the note sustains, and saturates gently at the top. It follows vibrato, bends and legato, collapses when you mute the strings and rebuilds on every new pick attack.",
        "The pedal is entirely digital and no schematic exists; FreakOut was built from the original manual, reviews (Sound On Sound, MusicRadar, Tape Op, Guitar Interactive) and the documented behaviour of each control."],
 inst_t="Installation",
 inst=[("Windows", "Copy <b>FreakOut.vst3</b> to <b>C:\\Program Files\\Common Files\\VST3</b>. In Reaper: Options &gt; Preferences &gt; Plug-ins &gt; VST &gt; <i>Re-scan</i>. The plugin appears as <b>FreakOut (Studio Wiga)</b>."),
       ("macOS", "Unzip the archive and copy <b>FreakOut.vst3</b> to <b>/Library/Audio/Plug-Ins/VST3</b> (or ~/Library/Audio/Plug-Ins/VST3). The build is universal (Apple Silicon and Intel) and ad hoc signed; if macOS blocks it, run <i>xattr -cr</i> on the file. Then re-scan in Reaper.")],
 ov_t="Interface overview",
 ov_rows=[["#", "Control", "Role"], ["1", "FEEDBACK ladder", "9 red LEDs: centre = effect on; pairs light from the centre outward as the feedback builds"],
          ["2", "MOMENTARY", "On: feedback only while the footswitch is held. Off: latching footswitch"],
          ["3", "DRY", "On: guitar + feedback. Off: feedback only (swell effect)"],
          ["4", "RANGE – GAIN", "Centre cap of the concentric knob: amount of feedback"],
          ["5", "RANGE – ONSET", "Knurled outer skirt: time for the feedback to reach full strength"],
          ["6", "TYPE", "Which harmonic feeds back: SUB, 1ST, 2ND, 3RD, 5TH, NAT LOW, NAT HI"],
          ["7", "Display", "Value of the control you touch, otherwise the harmonic, frequency and note feeding back"],
          ["8", "Preset bar", "Arrows = previous / next preset, name = preset menu"],
          ["9", "Footswitch", "Effect on / off (or hold, in momentary mode)"],
          ["10 / 11", "OUT / IN", "Output on the left, input on the right, as on a real pedal"]],
 ctl_t="Controls",
 ctl=[("RANGE – GAIN (centre cap)", "Sets how loud the feedback becomes, relative to the attack of the note you played: from nothing to about +6 dB above the attack. At 12 o'clock the feedback settles a little below the attack level. Higher settings give more sustain and a slightly richer, saturated tone at the top of the bloom."),
      ("RANGE – ONSET (outer skirt)", "Sets the time the feedback takes to reach full strength: 30 ms fully counter-clockwise, about 0.4 s at 12 o'clock, 6 s fully clockwise. The growth is exponential, like real feedback: nothing for a moment, then a fast swell. Drag the cap for GAIN and the skirt for ONSET; double-click returns to 12 o'clock, Shift gives fine adjustment."),
      ("TYPE", "Chooses the harmonic that feeds back. A harmonic is a multiple of the frequency of your note: <b>1ST</b> = unison, <b>2ND</b> = octave up, <b>3RD</b> = octave and a fifth, <b>5TH</b> = two octaves and a major third, <b>SUB</b> = one octave below (on notes too low to be heard an octave down, it falls back to unison). <b>NAT LOW</b> and <b>NAT HI</b> act like natural feedback: the harmonic depends on what is really ringing in the string, so the same note can feed back differently each time. NAT LOW stays in the lower harmonics (never the 5th or above, so no major third over minor chords); NAT HI reaches higher partials and jumps from one partial to another more readily, typically dropping lower as the note decays. Turning TYPE while it feeds back moves the feedback to the new harmonic."),
      ("Steering the natural types", "As with the pedal: play softer, roll the tone down or use the neck pickup for lower feedback; pick harder, open the tone or use the bridge pickup for higher feedback."),
      ("DRY", "On: your guitar passes through with the feedback added on top. Off: while the effect is on, only the feedback is heard, so every note swells in from silence, like an EBow or a theremin. When the effect is bypassed the dry signal always passes.")],
 knobs_cap="RANGE (GAIN cap and ONSET skirt) and TYPE: only the white markers turn, in the slight perspective of the pedal.",
 fs_t="Footswitch, LEDs and MIDI",
 fs=["<b>Footswitch</b>: click to switch the effect on or off; the button goes down into its nut while you press. With <b>MOMENTARY ON</b> the effect is only active while you hold the mouse button (or the MIDI footswitch). Engaging the effect while a note rings starts the feedback from that note.",
     "<b>LED ladder</b>: the centre LED shows that the effect is on; the others light in pairs from the centre outward as the feedback builds, so the ladder shows the onset in real time. The light of the LEDs falls on the panel around them.",
     "<b>Trigger sensitivity</b> (right-click menu): Low for hot pickups or noisy rooms, Normal, High for low-output pickups and soft playing.",
     "Every parameter can be automated and assigned to MIDI. Expression pedals are smoothed (ramp between messages, 14-bit CC accepted, pedal travel learnt automatically, small dead zones at both ends)."],
 ctl_cap="MOMENTARY / DRY switches (lever up = ON), and the footswitch released / pressed.",
 led_cap="The LED ladder: effect on (centre LED), bypass, feedback building, feedback at full strength.",
 midi_hdr=["MIDI", "Function"],
 midi=[["CC 20", "Gain (CC 52 = fine, 14-bit)"], ["CC 21", "Onset (CC 53 = fine, 14-bit)"], ["CC 22", "Type (0-127 split into 7 zones)"],
       ["CC 23", "Momentary (64 and above = on)"], ["CC 24", "Dry (64 and above = on)"],
       ["CC 25", "Footswitch: latching (127 on / 0 off) or toggle on each press (right-click > Settings); with MOMENTARY ON it acts while held"],
       ["Program Change 1-13", "Factory presets"]],
 win_t="Window and display",
 win=["The window opens at the same default size as the other Studio Wiga single pedals (391 × 493) and cannot be made smaller; drag the corner to enlarge it (proportions locked) or right-click > <i>Window size</i> (default, 75 %, 100 %).",
      "The display shows the value of the control you just moved for 2.5 s. The rest of the time it shows what is feeding back: harmonic (H1, H2… or SUB), frequency and note name. The preset bar below works like the pedal's menu: arrows for previous / next, click the name for the full list (factory presets, 9 user slots to load or save)."],
 disp_cap="Display: live feedback information, then the value of the control being moved.",
 pre_t="Presets",
 pre=["13 factory presets are included (also in the DAW preset menu and on MIDI Program Change). Nine user slots can be saved from the preset menu."],
 fac_hdr=["#", "Preset", "Gain / Onset", "Type", "Switches", "Description"],
 tips_t="Playing tips",
 tips=["In Reaper put FreakOut first in the chain, before Two Notes Genome or any amp simulator, as the original manual recommends: it needs the clean guitar signal to read the harmonics. A compressor or buffer before it is fine.",
       "Mute the strings you are not playing: like real feedback, unwanted ringing strings make the feedback less predictable.",
       "Low tunings: the pitch detection follows down to 30 Hz, so a 7-string tuned to G (49 Hz) or lower works. On very low notes, 2ND or 3RD usually gives the most audible feedback; SUB falls back to unison below about 50 Hz.",
       "For EBow / theremin lines: DRY off, 1ST or 3RD, onset around 1 o'clock, then add delay and reverb after the amp simulator.",
       "For classic \"amp on 11\" feedback on held notes: NAT HI, gain 1 o'clock, onset 11 o'clock, MOMENTARY on and press when you want the note to scream."],
 tech_t="Technical specifications",
 tech=[["Format", "VST3, Windows (x64) and macOS (Apple Silicon + Intel)"], ["Channels", "Mono or stereo (the detection uses the sum of the channels)"],
       ["Latency", "None (0 samples)"], ["Pitch range", "30 Hz to 1.6 kHz fundamental, feedback up to 8.5 kHz"],
       ["Engine", "YIN pitch detection, phase-locked harmonic oscillators, exponential bloom, natural partial selection"],
       ["Sample rates", "44.1 to 192 kHz"], ["Window", "391 × 493 default and minimum, up to 1140 × 1437"],
       ["MIDI", "CC 20-25 (14-bit on CC 52-53), Program Change 1-13"]],
)
T["fr"] = dict(
 lang="FRANÇAIS", sub="Générateur de larsen harmonique naturel", ver="Notice d'utilisation · version " + VERSION,
 intro_t="Présentation",
 intro=["<b>FreakOut</b> est un plugin VST3 Studio Wiga qui crée un larsen de guitare naturel à n'importe quel volume, en son clair ou saturé, au casque ou avec un simulateur d'ampli à faible volume. Il reprend le fonctionnement de la <b>DigiTech FreqOut</b> (Natural Feedback Creator) : mêmes commandes, mêmes sept types de larsen, même échelle de LED, mêmes inverseurs MOMENTARY et DRY.",
        "Comme la pédale, FreakOut suit la note jouée, choisit l'harmonique de cette note qui va larsener, puis la fait « fleurir » : le larsen part inaudible, grossit de façon exponentielle tant que la note tient, puis sature en douceur. Il suit le vibrato, les tirés et le legato, retombe quand on étouffe les cordes et se reconstruit à chaque nouvelle attaque.",
        "La pédale est entièrement numérique et aucun schéma n'existe : FreakOut a été construit d'après le manuel d'origine, les tests (Sound On Sound, MusicRadar, Tape Op, Guitar Interactive) et le comportement documenté de chaque commande."],
 inst_t="Installation",
 inst=[("Windows", "Copiez <b>FreakOut.vst3</b> dans <b>C:\\Program Files\\Common Files\\VST3</b>. Dans Reaper : Options &gt; Preferences &gt; Plug-ins &gt; VST &gt; <i>Re-scan</i>. Le plugin apparaît sous le nom <b>FreakOut (Studio Wiga)</b>."),
       ("macOS", "Décompressez l'archive et copiez <b>FreakOut.vst3</b> dans <b>/Library/Audio/Plug-Ins/VST3</b> (ou ~/Library/Audio/Plug-Ins/VST3). La version est universelle (Apple Silicon et Intel) et signée localement ; si macOS la bloque, lancez <i>xattr -cr</i> sur le fichier. Relancez ensuite le scan dans Reaper.")],
 ov_t="Vue d'ensemble",
 ov_rows=[["N°", "Commande", "Rôle"], ["1", "Échelle FEEDBACK", "9 LED rouges : centre = effet actif ; les paires s'allument du centre vers l'extérieur pendant la montée du larsen"],
          ["2", "MOMENTARY", "On : larsen seulement tant que le footswitch est enfoncé. Off : footswitch à accrochage"],
          ["3", "DRY", "On : guitare + larsen. Off : larsen seul (effet de gonflement)"],
          ["4", "RANGE – GAIN", "Chapeau central du potard concentrique : quantité de larsen"],
          ["5", "RANGE – ONSET", "Jupe moletée : temps pour que le larsen atteigne sa pleine intensité"],
          ["6", "TYPE", "Harmonique qui larsène : SUB, 1ST, 2ND, 3RD, 5TH, NAT LOW, NAT HI"],
          ["7", "Écran", "Valeur de la commande touchée, sinon l'harmonique, la fréquence et la note qui larsènent"],
          ["8", "Barre de presets", "Flèches = preset précédent / suivant, nom = menu des presets"],
          ["9", "Footswitch", "Effet en marche / coupé (ou maintenu, en mode momentané)"],
          ["10 / 11", "OUT / IN", "Sortie à gauche, entrée à droite, comme sur une vraie pédale"]],
 ctl_t="Les commandes",
 ctl=[("RANGE – GAIN (chapeau central)", "Règle le volume que prend le larsen, par rapport à l'attaque de la note jouée : de rien à environ +6 dB au-dessus de l'attaque. À 12 h, le larsen se stabilise un peu sous le niveau de l'attaque. Plus haut, on gagne en sustain et le haut de la montée devient un peu plus riche et saturé."),
      ("RANGE – ONSET (jupe)", "Règle le temps que met le larsen pour atteindre sa pleine intensité : 30 ms tout à gauche, environ 0,4 s à 12 h, 6 s tout à droite. La croissance est exponentielle, comme un vrai larsen : rien pendant un instant, puis un gonflement rapide. Tirez le chapeau pour GAIN et la jupe pour ONSET ; double-clic = retour à 12 h, Maj = réglage fin."),
      ("TYPE", "Choisit l'harmonique qui larsène. Une harmonique est un multiple de la fréquence de la note : <b>1ST</b> = unisson, <b>2ND</b> = octave au-dessus, <b>3RD</b> = octave + quinte, <b>5TH</b> = deux octaves + tierce majeure, <b>SUB</b> = octave en dessous (sur les notes trop graves pour être entendues une octave plus bas, il repasse à l'unisson). <b>NAT LOW</b> et <b>NAT HI</b> se comportent comme un larsen naturel : l'harmonique dépend de ce qui sonne réellement dans la corde, la même note peut donc larsener différemment à chaque fois. NAT LOW reste dans les harmoniques basses (jamais la 5e ni au-dessus, donc pas de tierce majeure sur un accord mineur) ; NAT HI monte plus haut et saute plus volontiers d'un partiel à l'autre, en redescendant en général quand la note s'éteint. Tourner TYPE pendant que ça larsène fait passer le larsen sur la nouvelle harmonique."),
      ("Orienter les types naturels", "Comme sur la pédale : jouez plus doucement, baissez la tonalité ou prenez le micro manche pour un larsen plus grave ; attaquez plus fort, ouvrez la tonalité ou prenez le micro chevalet pour un larsen plus aigu."),
      ("DRY", "On : la guitare passe, le larsen s'ajoute par-dessus. Off : quand l'effet est en marche, on n'entend que le larsen, chaque note arrive donc en gonflant depuis le silence, comme avec un EBow ou un thérémine. Effet coupé, le signal sec passe toujours.")],
 knobs_cap="RANGE (chapeau GAIN et jupe ONSET) et TYPE : seuls les repères blancs tournent, dans la légère perspective de la pédale.",
 fs_t="Footswitch, LED et MIDI",
 fs=["<b>Footswitch</b> : un clic met l'effet en marche ou le coupe ; le bouton descend dans son écrou tant qu'on appuie. Avec <b>MOMENTARY ON</b>, l'effet n'agit que tant que le bouton de la souris (ou le footswitch MIDI) est maintenu. Enclencher l'effet pendant qu'une note sonne fait partir le larsen de cette note.",
     "<b>Échelle de LED</b> : la LED du centre indique que l'effet est en marche ; les autres s'allument par paires du centre vers l'extérieur pendant que le larsen monte : l'échelle montre la montée en temps réel. La lumière des LED éclaire la façade autour d'elles.",
     "<b>Sensibilité de déclenchement</b> (menu clic droit) : Low pour les micros puissants ou une pièce bruyante, Normal, High pour les micros faibles et un jeu léger.",
     "Tous les paramètres sont automatisables et assignables en MIDI. Les pédales d'expression sont lissées (rampe entre deux messages, CC 14 bits accepté, course de la pédale apprise automatiquement, petites zones mortes aux deux butées)."],
 ctl_cap="Inverseurs MOMENTARY / DRY (levier en haut = ON), et footswitch relâché / enfoncé.",
 led_cap="L'échelle de LED : effet en marche (LED centrale), bypass, larsen qui monte, larsen à pleine intensité.",
 midi_hdr=["MIDI", "Fonction"],
 midi=[["CC 20", "Gain (CC 52 = fin, 14 bits)"], ["CC 21", "Onset (CC 53 = fin, 14 bits)"], ["CC 22", "Type (0-127 découpé en 7 zones)"],
       ["CC 23", "Momentary (64 et plus = on)"], ["CC 24", "Dry (64 et plus = on)"],
       ["CC 25", "Footswitch : à accrochage (127 on / 0 off) ou bascule à chaque appui (clic droit > Settings) ; avec MOMENTARY ON il agit tant qu'il est enfoncé"],
       ["Program Change 1-13", "Presets d'usine"]],
 win_t="Fenêtre et écran",
 win=["La fenêtre s'ouvre à la même taille par défaut que les autres pédales simples Studio Wiga (391 × 493) et ne peut pas être réduite en dessous ; tirez le coin pour l'agrandir (proportions bloquées) ou clic droit > <i>Window size</i> (défaut, 75 %, 100 %).",
      "L'écran affiche pendant 2,5 s la valeur de la commande que vous venez de bouger. Le reste du temps, il montre ce qui larsène : harmonique (H1, H2… ou SUB), fréquence et nom de la note. La barre de presets dessous fonctionne comme un menu de pédale : flèches pour précédent / suivant, clic sur le nom pour la liste complète (presets d'usine, 9 emplacements utilisateur à charger ou enregistrer)."],
 disp_cap="Écran : informations du larsen en direct, puis valeur de la commande qu'on règle.",
 pre_t="Presets",
 pre=["13 presets d'usine sont fournis (aussi dans le menu de presets du DAW et en Program Change MIDI). Neuf emplacements utilisateur peuvent être enregistrés depuis le menu des presets."],
 fac_hdr=["N°", "Preset", "Gain / Onset", "Type", "Inverseurs", "Description"],
 tips_t="Conseils d'utilisation",
 tips=["Dans Reaper, placez FreakOut en premier dans la chaîne, avant Two Notes Genome ou tout simulateur d'ampli, comme le recommande le manuel d'origine : il a besoin du signal clair de la guitare pour lire les harmoniques. Un compresseur ou un buffer avant lui ne pose pas de problème.",
       "Étouffez les cordes que vous ne jouez pas : comme pour un vrai larsen, des cordes qui sonnent à vide rendent le larsen moins prévisible.",
       "Accordages graves : la détection descend jusqu'à 30 Hz, une 7 cordes en G (49 Hz) ou plus bas fonctionne. Sur les notes très graves, 2ND ou 3RD donnent en général le larsen le plus audible ; SUB repasse à l'unisson sous environ 50 Hz.",
       "Pour des lignes façon EBow / thérémine : DRY coupé, 1ST ou 3RD, onset vers 13 h, puis delay et reverb après le simulateur d'ampli.",
       "Pour le larsen « ampli à fond » sur les notes tenues : NAT HI, gain à 13 h, onset à 11 h, MOMENTARY ON, et appuyez quand la note doit hurler."],
 tech_t="Caractéristiques techniques",
 tech=[["Format", "VST3, Windows (x64) et macOS (Apple Silicon + Intel)"], ["Canaux", "Mono ou stéréo (la détection utilise la somme des canaux)"],
       ["Latence", "Aucune (0 échantillon)"], ["Plage de hauteur", "Fondamentale de 30 Hz à 1,6 kHz, larsen jusqu'à 8,5 kHz"],
       ["Moteur", "Détection de hauteur YIN, oscillateurs harmoniques verrouillés en phase, montée exponentielle, choix naturel des partiels"],
       ["Fréquences", "44,1 à 192 kHz"], ["Fenêtre", "391 × 493 par défaut et au minimum, jusqu'à 1140 × 1437"],
       ["MIDI", "CC 20 à 25 (14 bits sur CC 52-53), Program Change 1 à 13"]],
)

W = A4[0] - 36 * mm
def section(lang):
    t = T[lang]; f = []
    f.append(NextPageTemplate("p"))
    f.append(Spacer(1, 2 * mm))
    f.append(P(t["lang"], "coverlang"))
    f.append(Spacer(1, 2 * mm))
    f.append(img(cover, W * 0.56))
    f.append(Spacer(1, 2 * mm))
    f.append(P("F R E A K O U T", "cover"))
    f.append(P(t["sub"], "coversub"))
    f.append(Spacer(1, 2 * mm))
    f.append(P(t["ver"], "coversub"))
    f.append(Spacer(1, 6 * mm))
    f.append(img(os.path.join(ROOT, "assets", "logo_studio_wiga_blanc.png"), 52 * mm))
    f.append(PageBreak())
    f.append(P(t["intro_t"], "h1")); f += [P(x) for x in t["intro"]]
    f.append(P(t["inst_t"], "h1"))
    for h, x in t["inst"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(PageBreak())
    f.append(P(t["ov_t"], "h1"))
    ov = Table([[img(annotated, 92 * mm), table(t["ov_rows"], [11 * mm, 25 * mm, W - 96 * mm - 36 * mm])]], colWidths=[96 * mm, W - 96 * mm])
    ov.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("LEFTPADDING", (0, 0), (-1, -1), 0)]))
    f.append(ov)
    f.append(PageBreak())
    f.append(P(t["ctl_t"], "h1"))
    for h, x in t["ctl"]: f.append(P(h, "h2")); f.append(P(x))
    f.append(KeepTogether([Spacer(1, 3 * mm), img(knobs_img, W * 0.46), P(t["knobs_cap"], "small")]))
    f.append(P(t["fs_t"], "h1")); f += bullets(t["fs"])
    f.append(KeepTogether([Spacer(1, 2 * mm), img(ctl_img, W * 0.66), P(t["ctl_cap"], "small")]))
    f.append(KeepTogether([Spacer(1, 3 * mm), img(leds_img, W * 0.42), P(t["led_cap"], "small")]))
    f.append(Spacer(1, 3 * mm))
    f.append(KeepTogether([table([t["midi_hdr"]] + t["midi"], [42 * mm, W - 42 * mm])]))
    f.append(P(t["win_t"], "h1")); f += [P(x) for x in t["win"]]
    f.append(KeepTogether([img(disp_img, W * 0.92), P(t["disp_cap"], "small")]))
    f.append(CondPageBreak(150 * mm))
    f.append(P(t["pre_t"], "h1")); f += [P(x) for x in t["pre"]]
    rows = [t["fac_hdr"]]
    for i, (n, g, o, ty, mo, dr_) in enumerate(FACTORY):
        sw = ("MOM " + ("on" if mo else "off")) + " · DRY " + ("on" if dr_ else "off")
        rows.append([str(i + 1), "<b>" + n + "</b>", f"{pc(g)} / {pc(o)}<br/>({onset_s(o)})", TYPES[ty], sw, FACT_DESC[lang][i]])
    f.append(table(rows, [9 * mm, 30 * mm, 22 * mm, 16 * mm, 22 * mm, W - 99 * mm]))
    f.append(PageBreak())
    f.append(P(t["tips_t"], "h1")); f += bullets(t["tips"])
    f.append(KeepTogether([P(t["tech_t"], "h1"), table(t["tech"], [32 * mm, W - 32 * mm], header=False)]))
    f.append(PageBreak())
    return f

def deco(c, doc):
    c.saveState()
    c.setFillColor(DARK); c.rect(0, A4[1] - 6 * mm, A4[0], 6 * mm, stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 6.8 * mm, A4[0], 0.8 * mm, stroke=0, fill=1)
    c.setFont("Pop", 7.5); c.setFillColor(GREY)
    c.drawString(18 * mm, 10 * mm, "FreakOut (Studio Wiga) · v" + VERSION)
    c.drawRightString(A4[0] - 18 * mm, 10 * mm, str(doc.page))
    c.restoreState()

doc = BaseDocTemplate(OUT, pagesize=A4, leftMargin=18 * mm, rightMargin=18 * mm, topMargin=16 * mm, bottomMargin=18 * mm,
                      title="FreakOut - Manual / Notice v" + VERSION, author="Studio Wiga")
def cover_bg(c, doc):
    c.saveState(); c.setFillColor(colors.black); c.rect(0, 0, A4[0], A4[1], stroke=0, fill=1)
    c.setFillColor(ACC); c.rect(0, A4[1] - 3 * mm, A4[0], 3 * mm, stroke=0, fill=1); c.restoreState()
doc.addPageTemplates([PageTemplate(id="cover", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="fc")], onPage=cover_bg),
                      PageTemplate(id="p", frames=[Frame(doc.leftMargin, doc.bottomMargin, doc.width, doc.height, id="f")], onPage=deco)])
en = section("en"); fr = section("fr")
en[-1:] = [NextPageTemplate("cover"), PageBreak()]
story = en + fr[:-1]
doc.build(story)
print(OUT)
