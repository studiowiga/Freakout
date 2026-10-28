"""
FreakOut (Studio Wiga) - fond du plugin a partir du visuel fourni (design/source/freakout_visuel.png).

Le visuel est la reference : son eclairage (lumiere douce venant du haut, legerement a gauche) est conserve.
Ce script, dans le repere de la photo (928 x 1152), puis mis a l'echelle du cadre 1520 x 1916 (= 2 x 760 x 958) :
  1. efface les reperes blancs des potards (ils sont redessines et tournent dans le plugin, en perspective),
  2. retire les deux inverseurs (remplaces par des sprites tires de la photo : levier haut = ON, levier bas = OFF),
  3. eteint les 9 LED (lentille rouge sombre synthetisee, reflets rouges retires de la bague et de la facade)
     et calcule les calques d'eclairage « allume » a partir de la lumiere reelle d'une LED de la photo,
  4. vide l'ecran (textes effaces, cadre et barre de presets gardes) : le plugin y ecrit,
  5. prepare le footswitch enfonce (bouton descendu dans son ecrou, ombre du bord),
  6. ecrit Layout.h (reperes en unites 760 x 958, ellipses de perspective des potards).

Usage : python3 tools/make_background.py
"""
import os, json, math
import numpy as np, cv2
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SRC = os.path.join(ROOT, 'design', 'source', 'freakout_visuel.png')

A0 = cv2.cvtColor(cv2.imread(SRC), cv2.COLOR_BGR2RGB).astype(np.float32)
H, W = A0.shape[:2]
A = A0.copy()
YY, XX = np.mgrid[0:H, 0:W].astype(np.float32)
lum = lambda a: a.mean(-1)

# ------------------------------------------------------------------ reperes mesures sur la photo (px)
LED_X, LED_Y0, LED_DY = 279.6, 125.0, 45.85
LEDS = [(LED_X, LED_Y0 + i * LED_DY) for i in range(9)]
LENS_R, BEZEL_R = 9.6, 14.0
CAP = dict(c=(618.5, 197.0), rx=39.0, ry=38.0)            # face superieure du chapeau GAIN
CAPBASE = (619.5, 214.0); CAP_SIDE_R = 41.5                # base du chapeau (sur la jupe)
SKIRT = dict(c=(620.5, 203.0), rx=66.5, ry=65.0)          # dessus de la jupe ONSET
TYPEK = dict(c=(621.0, 425.0), rx=46.0, ry=44.0)          # dessus du selecteur TYPE
TOG = {'momentary': (406.0, 211.0), 'dry': (406.0, 435.0)}  # centre des bagues ; photo : momentary = bas (OFF), dry = haut (ON)
FS = dict(c=(466.0, 897.0), r=45.5, base=(467.0, 915.0))   # dessus du bouton du footswitch, base de la jupe moletee
DISP_TEXT = (226, 562, 706, 650)                           # interieur de l'ecran (textes a effacer)
BAR = (229.0, 615.5, 703.0, 648.0)                         # barre de presets (cadre garde)
ROW = (232.0, 566.0, 700.0, 610.0)                         # ligne du haut (etat / larsen)

def inpaint(mask, radius=3, img=None):
    global A
    src = np.clip(A if img is None else img, 0, 255).astype(np.uint8)
    out = cv2.inpaint(src, mask.astype(np.uint8), radius, cv2.INPAINT_TELEA).astype(np.float32)
    if img is None: A = out
    return out

# ------------------------------------------------------------------ 1. reperes des potards
def pointer_mask(box, thr=110):
    x0, y0, x1, y1 = box
    m = np.zeros((H, W), np.uint8)
    reg = A[y0:y1, x0:x1]
    m[y0:y1, x0:x1] = ((lum(reg) > thr) & ((reg.max(-1) - reg.min(-1)) < 40)).astype(np.uint8)
    return cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (5, 5)))

def knob_inpaint(box, e, margin=1.5):
    """efface un repere sans faire couler le metal voisin : hors de l'ellipse du potard, l'image source est noircie"""
    global A
    cx, cy = e['c']
    inside = (((XX - cx) / (e['rx'] - margin)) ** 2 + ((YY - cy) / (e['ry'] - margin)) ** 2) <= 1.0
    m = pointer_mask(box) * inside
    tmp = A.copy()
    dark = np.median(A[inside & (lum(A) < 60)], 0)
    tmp[~inside] = dark
    out = inpaint(m, 4, img=tmp)
    A = np.where(m[..., None] > 0, out, A)

knob_inpaint((616, 160, 645, 196), CAP)
knob_inpaint((594, 132, 614, 165), SKIRT, margin=1.0)
# bout du repere sur le chanfrein de la jupe : repris sur le chanfrein lui-meme
_cx, _cy = SKIRT['c']
_q = np.sqrt(((XX - _cx) / SKIRT['rx']) ** 2 + ((YY - _cy) / SKIRT['ry']) ** 2)
_m = pointer_mask((594, 128, 612, 146), thr=150) * ((_q > 0.95) & (_q < 1.06))
inpaint(_m, 2)
knob_inpaint((620, 425, 656, 463), TYPEK)
# lissage tres doux des zones reprises (la face noire est mate) puis grain de la photo
rng = np.random.default_rng(4)

# ------------------------------------------------------------------ 2. inverseurs : sprites puis effacement
TW, TH0, TH1 = 56, 50, 88          # demi-largeur, au-dessus, en dessous du centre de la bague
def toggle_crop(c):
    x, y = int(round(c[0])), int(round(c[1]))
    return A0[y - TH0:y + TH1, x - TW:x + TW].copy()
tog_off = toggle_crop(TOG['momentary'])      # levier bas
tog_on = toggle_crop(TOG['dry'])             # levier haut
# masque du sprite : bague + levier + ombres (forme douce) ; ailleurs la facade de la photo
sy, sx = np.mgrid[-TH0:TH1, -TW:TW].astype(np.float32)
ring_m = np.maximum(np.clip((33.0 - np.hypot(sx, sy * 1.02)) / 5.0, 0, 1), np.clip((45.0 - np.hypot(sx - 5, sy - 9)) / 9.0, 0, 1))
ring_m = np.maximum(ring_m, np.clip(1.0 - np.hypot((sx - 26) / 28.0, (sy - 58) / 18.0), 0, 1) * 1.6)     # bout de l'ombre du levier bas
ring_m = np.maximum(ring_m, np.clip(1.0 - np.hypot((sx - 12) / 40.0, (sy - 66) / 20.0), 0, 1) * 1.6)
lever_up = np.clip((9.5 - np.abs(sx)) / 4.0, 0, 1) * np.clip((sy + 49) / 5.0, 0, 1) * (sy < 0)
lever_dn = np.clip((9.5 - np.abs(sx)) / 4.0, 0, 1) * np.clip((36 - sy) / 5.0, 0, 1) * (sy > 0)
spr_mask = np.clip(np.maximum(ring_m, np.maximum(lever_up, lever_dn)), 0, 1)
# les inscriptions (OFF) ne viennent jamais du sprite : celles du fond restent nettes
body_c = (np.hypot(sx, sy) < 37) | ((np.abs(sx) < 15) & (sy > -47) & (sy < 36))
txt = np.zeros_like(spr_mask)
for crop in (tog_on, tog_off):
    t_ = ((lum(crop) > 85) & ~body_c).astype(np.uint8)
    txt = np.maximum(txt, cv2.dilate(t_, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))).astype(np.float32))
spr_mask = np.clip(spr_mask, 0, 1) * (1 - txt)
spr_mask = cv2.GaussianBlur(spr_mask, (0, 0), 1.2)

# facade noire sous les inverseurs : basses frequences reprises des alentours (moyenne normalisee hors masque),
# grain fin de la serigraphie repris d'une zone propre du meme panneau (entre OFF et la ligne DRY)
def clean_fill(m, donor_box, fit_box):
    """remplit le masque : plan de luminosite ajuste sur l'encre propre autour + grain fin d'une zone donneuse"""
    global A
    fx0, fy0, fx1, fy1 = fit_box
    ys, xs = np.mgrid[fy0:fy1, fx0:fx1]
    reg = A0[fy0:fy1, fx0:fx1]
    L_ = lum(reg)
    ok = (m[fy0:fy1, fx0:fx1] == 0) & (L_ < 70)
    ok &= L_ > np.median(L_[ok]) - 6                                   # pas les ombres
    X = np.stack([np.ones(ok.sum()), xs[ok], ys[ok]], 1).astype(np.float64)
    plane = np.zeros((H, W, 3), np.float32)
    for c in range(3):
        coef, *_ = np.linalg.lstsq(X, reg[..., c][ok].astype(np.float64), rcond=None)
        plane[..., c] = (coef[0] + coef[1] * XX + coef[2] * YY).astype(np.float32)
    x0, y0, x1, y1 = donor_box
    donor = A0[y0:y1, x0:x1]
    hf = donor - cv2.GaussianBlur(donor, (0, 0), 3.0)
    grain = np.tile(hf, (int(np.ceil(H / hf.shape[0])), int(np.ceil(W / hf.shape[1])), 1))[:H, :W]
    mm = cv2.GaussianBlur(m.astype(np.float32), (0, 0), 1.5)[..., None]
    A = A * (1 - mm) + (plane + grain) * mm

for c in TOG.values():
    x, y = int(round(c[0])), int(round(c[1]))
    m = np.zeros((H, W), np.uint8)
    cv2.ellipse(m, (x + 6, y + 10), (50, 50), 0, 0, 360, 1, -1)          # bague, levier bas et ombre portee
    cv2.ellipse(m, (x + 26, y + 58), (28, 18), 0, 0, 360, 1, -1)        # bout de l'ombre du levier bas
    cv2.ellipse(m, (x + 12, y + 66), (40, 20), 0, 0, 360, 1, -1)
    cv2.rectangle(m, (x - 16, y - 47), (x + 20, y + 40), 1, -1)        # levier haut
    # les inscriptions (ON / OFF) et le bord du panneau sont gardes
    body = np.zeros((H, W), np.uint8)
    cv2.ellipse(body, (x, y), (37, 37), 0, 0, 360, 1, -1)
    cv2.rectangle(body, (x - 15, y - 47), (x + 15, y + 36), 1, -1)
    text = cv2.dilate(((lum(A0) > 85)).astype(np.uint8), cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7))) * (1 - body)
    m = m * (1 - text)
    m[:, x + 60:] = 0
    m[:, :x - 48] = 0
    clean_fill(m, (357, 226, 378, 294), (x - 52, y - 64, x + 62, y + 80))

# ------------------------------------------------------------------ 3. LED : extinction
lit_ref_i = 3                                  # LED de reference (bien allumee, centre de la colonne)
col = (slice(90, 530), slice(236, 340))
reg = A[col]
# lumiere rouge retiree de la facade, des graduations et des bagues : rouge ramene au niveau du vert / bleu
neutral = (reg[..., 1] + reg[..., 2]) * 0.5
red_ex = np.clip(reg[..., 0] - neutral * 1.04, 0, None)
reg[..., 0] = reg[..., 0] - red_ex
# l'encre noire est aussi eclaircie par le halo : on retire l'exces de luminance pres des LED
A[col] = reg
for (lx, ly) in LEDS:
    d = np.hypot(XX - lx, YY - ly)
    halo = (d > BEZEL_R + 1) & (d < 34) & (lum(A) < 85)   # l'encre seulement (pas les graduations argent)
    loc = A[halo]
    base_lvl = np.array([40.0, 40.0, 42.0])
    k = np.clip((34 - d[halo]) / 20.0, 0, 1)[..., None]
    A[halo] = loc * (1 - 0.55 * k) + np.minimum(loc, base_lvl) * 0.55 * k

def lens_off(cx, cy):
    """lentille eteinte : dome rouge sombre translucide, reflet net de la lumiere du haut (legerement a gauche)"""
    d = np.hypot(XX - cx, YY - cy)
    m = np.clip(LENS_R + 0.5 - d, 0, 1)
    u = np.clip(d / LENS_R, 0, 1)
    base = np.dstack([58 - 26 * u, 9 + 3 * (1 - u), 11 + 2 * (1 - u)])
    # le fond de la lentille laisse voir la puce (un peu plus sombre au centre)
    base = base * (0.85 + 0.15 * u)[..., None]
    # reflet principal et petit reflet secondaire
    hx, hy = cx - 2.6, cy - 4.3
    sp = np.exp(-(((XX - hx) / 2.6) ** 2 + ((YY - hy) / 1.6) ** 2)) * 190
    sp2 = np.exp(-(((XX - cx - 3.2) / 2.0) ** 2 + ((YY - cy - 4.6) / 1.0) ** 2)) * 38
    rim = np.clip(1 - np.abs(d - LENS_R + 1.2) / 1.2, 0, 1) * 24
    lens = base + (sp + sp2)[..., None] + (rim[..., None] * np.array([1.0, 0.35, 0.35]))
    return m, lens

for (lx, ly) in LEDS:
    # bague chromee : couleur neutre (plus de reflet rouge), gorge sombre entre bague et lentille
    d = np.hypot(XX - lx, YY - ly)
    bez = (d < BEZEL_R + 1.0)
    A[bez] = lum(A[bez])[..., None] * np.array([0.985, 1.0, 1.02], np.float32)
    gap = np.clip(1 - np.abs(d - 11.0) / 1.3, 0, 1)[..., None]
    A[:] = A * (1 - 0.8 * gap) + np.array([22, 22, 24], np.float32) * 0.8 * gap
    m, lens = lens_off(lx, ly)
    A[:] = A * (1 - m[..., None]) + lens * m[..., None]

OFF = A.copy()

# lumiere reelle d'une LED allumee : difference photo - eteinte autour de la LED de reference
rx_, ry_ = LEDS[lit_ref_i]
R_GL = 34
ix0, iy0 = int(round(rx_)) - R_GL, int(round(ry_)) - R_GL
diff_ref = (A0 - OFF)[iy0:iy0 + 2 * R_GL, ix0:ix0 + 2 * R_GL].copy()
gy, gx = np.mgrid[0:2 * R_GL, 0:2 * R_GL].astype(np.float32)
dd = np.hypot(gx + ix0 - rx_, gy + iy0 - ry_)
win = np.clip((R_GL - 2 - dd) / 12.0, 0, 1) ** 1.5           # fenetre douce : retombe a 0 avant le bord
# la lumiere des LED voisines (deja dans la photo) est retiree : seule la moitie proche de la LED compte
diff_ref *= win[..., None]
diff_ref = np.where(diff_ref > 0, diff_ref, diff_ref * 0.6)

# ------------------------------------------------------------------ 4. ecran : textes effaces
x0, y0, x1, y1 = DISP_TEXT
m = np.zeros((H, W), np.uint8)
sub = A[y0:y1, x0:x1]
m[y0:y1, x0:x1] = ((lum(sub) > 88) | ((sub[..., 0] - sub[..., 2] > 14) & (lum(sub) > 34))).astype(np.uint8)
m = cv2.dilate(m, cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (7, 7)))
inpaint(m, 6)
OFF = np.where((m > 0)[..., None], A, OFF)

# ------------------------------------------------------------------ 5. footswitch enfonce (dans le repere photo)
FSB = (396, 826, 538, 990)       # zone du footswitch
fx0, fy0, fx1, fy1 = FSB
up = A[fy0:fy1, fx0:fx1].copy()
cy_, cx_ = np.mgrid[fy0:fy1, fx0:fx1].astype(np.float32)
fcx, fcy = FS['c']; bcx, bcy = FS['base']
top_disc = np.hypot(cx_ - fcx, cy_ - fcy) <= FS['r'] + 1.5
# silhouette du bouton : enveloppe du disque du dessus et du disque de base (perspective)
side = (np.abs(cx_ - (fcx + bcx) / 2) <= FS['r'] + 1.5) & (cy_ >= fcy) & (cy_ <= bcy)
base_disc = np.hypot(cx_ - bcx, cy_ - bcy) <= FS['r'] + 1.5
cap = (top_disc | side | base_disc).astype(np.float32)
D = 3.0                           # course apparente du bouton (px photo)
# bouton descendu : decale vers le bas, tres legerement plus petit (il s'eloigne), coupe par sa base
shift = cv2.warpAffine(up, np.float32([[0.985, 0, (1 - 0.985) * (fcx - fx0)], [0, 0.985, (1 - 0.985) * (fcy - fy0) + D]]),
                       (fx1 - fx0, fy1 - fy0), flags=cv2.INTER_CUBIC, borderMode=cv2.BORDER_REPLICATE)
capm = cv2.warpAffine(cap, np.float32([[0.985, 0, (1 - 0.985) * (fcx - fx0)], [0, 0.985, (1 - 0.985) * (fcy - fy0) + D]]),
                      (fx1 - fx0, fy1 - fy0), flags=cv2.INTER_LINEAR)
capm = capm * cap                 # le bas de la jupe rentre dans l'ecrou
down = up.copy()
# zone liberee en haut du bouton : la bague moletee se voit davantage -> reprise par inpainting
freed = ((cap > 0.5) & (capm < 0.5)).astype(np.uint8)
down = inpaint(cv2.dilate(freed, np.ones((3, 3), np.uint8)), 3, img=down)
capm_s = cv2.GaussianBlur(capm, (0, 0), 0.7)[..., None]
down = down * (1 - capm_s) + shift * 0.93 * capm_s        # un peu moins de lumiere sur le dessus enfonce
# ombre portee du rebord de l'ecrou sur le haut du bouton enfonce
edge = np.clip(1 - (cy_ - (fcy - FS['r'] + D)) / 9.0, 0, 1) * capm
down = down * (1 - 0.30 * edge[..., None])
FS_DOWN = down

# ------------------------------------------------------------------ 6. mise a l'echelle du cadre (1520 x 1916)
S = 1916.0 / H
CROP = (W * S - 1520.0) / 2.0
def U(p): return ((p[0] * S - CROP) / 2.0, p[1] * S / 2.0)      # photo -> unites 760 x 958
def UL(v): return v * S / 2.0                                    # longueur photo -> unites

def upscale(img):
    big = cv2.resize(np.clip(img, 0, 255), (int(round(W * S)), 1916), interpolation=cv2.INTER_LANCZOS4)
    c0 = int(round(CROP))
    big = big[:, c0:c0 + 1520]
    # rehausse douce (la photo est agrandie de 1,66 x)
    blur = cv2.GaussianBlur(big, (0, 0), 1.4)
    return np.clip(big + 0.55 * (big - blur), 0, 255)

bg = upscale(OFF)
Image.fromarray(bg.astype(np.uint8)).save(os.path.join(ROOT, 'background.jpg'), quality=94, subsampling=0)

# calques d'eclairage de l'echelle de LED (5 etats cumulatifs : centre, puis paires vers l'exterieur)
order = [[4], [3, 5], [2, 6], [1, 7], [0, 8]]
GL = (LED_X - 40, LEDS[0][1] - 40, LED_X + 40, LEDS[-1][1] + 40)      # px photo
gx0, gy0, gx1, gy1 = [int(round(v)) for v in GL]
layers = []
lit = []
for st in order:
    lit += st
    add = np.zeros((gy1 - gy0, gx1 - gx0, 3), np.float32)
    for i in lit:
        lx, ly = LEDS[i]
        ox, oy = int(round(lx)) - R_GL - gx0, int(round(ly)) - R_GL - gy0
        add[oy:oy + 2 * R_GL, ox:ox + 2 * R_GL] += diff_ref
    full = np.zeros_like(OFF); full[gy0:gy1, gx0:gx1] = add
    lit_img = upscale(np.clip(OFF + full, 0, 255))
    add_big = lit_img - bg
    a = np.clip(np.abs(add_big).max(-1) / 10.0, 0, 1)
    rgb = np.where(a[..., None] > 1e-3, bg + add_big / np.maximum(a, 1e-3)[..., None], bg)
    ux0, uy0 = U((GL[0], GL[1])); ux1, uy1 = U((GL[2], GL[3]))
    X0, Y0, X1, Y1 = int(math.floor(ux0 * 2)), int(math.floor(uy0 * 2)), int(math.ceil(ux1 * 2)), int(math.ceil(uy1 * 2))
    layers.append(np.dstack([np.clip(rgb[Y0:Y1, X0:X1], 0, 255), a[Y0:Y1, X0:X1] * 255]).astype(np.uint8))
GLOW_RECT = (X0 / 2, Y0 / 2, (X1 - X0) / 2, (Y1 - Y0) / 2)
Image.fromarray(np.concatenate(layers, 0), 'RGBA').save(os.path.join(ROOT, 'glow_strip.png'), optimize=True)

# sprites des inverseurs (3 images : haut = ON, intermediaire (flou de mouvement), bas = OFF), 2 px / unite
def sprite(img_):
    big = cv2.resize(img_, (int(round(2 * TW * S)), int(round((TH0 + TH1) * S))), interpolation=cv2.INTER_LANCZOS4)
    mk = cv2.resize(spr_mask, (big.shape[1], big.shape[0]), interpolation=cv2.INTER_LINEAR)
    blur = cv2.GaussianBlur(big, (0, 0), 1.4); big = np.clip(big + 0.55 * (big - blur), 0, 255)
    return np.dstack([big, mk * 255])
s_on, s_off = sprite(tog_on), sprite(tog_off)
s_mid = s_on.copy(); s_mid[..., :3] = 0.5 * s_on[..., :3] + 0.5 * s_off[..., :3]; s_mid[..., 3] = np.maximum(s_on[..., 3], s_off[..., 3])
Image.fromarray(np.concatenate([s_on, s_mid, s_off], 0).astype(np.uint8), 'RGBA').save(os.path.join(ROOT, 'toggle_strip.png'), optimize=True)

# footswitch enfonce : sprite (zone du footswitch) a afficher tant qu'on appuie
fs_big = cv2.resize(np.clip(FS_DOWN, 0, 255), (int(round((fx1 - fx0) * S)), int(round((fy1 - fy0) * S))), interpolation=cv2.INTER_LANCZOS4)
blur = cv2.GaussianBlur(fs_big, (0, 0), 1.4); fs_big = np.clip(fs_big + 0.55 * (fs_big - blur), 0, 255)
# bord adouci (raccord invisible avec le fond)
my, mx = np.mgrid[0:fs_big.shape[0], 0:fs_big.shape[1]].astype(np.float32)
fcxs, fcys = (fcx - fx0) * S, (fcy - fy0 + 8) * S
fa = np.clip((62 * S - np.hypot(mx - fcxs, (my - fcys) * 0.92)) / (6 * S), 0, 1)
Image.fromarray(np.dstack([fs_big, fa * 255]).astype(np.uint8), 'RGBA').save(os.path.join(ROOT, 'fs_down.png'), optimize=True)
FS_RECT = (*U((fx0, fy0)), (fx1 - fx0) * S / 2, (fy1 - fy0) * S / 2)

# ------------------------------------------------------------------ 7. reperes pour le code
def P2(p): return '{ %.2ff, %.2ff }' % (p[0], p[1])
def ell(e): c = U(e['c']); return c, UL(e['rx']), UL(e['ry'])
cap_c, cap_rx, cap_ry = ell(CAP); sk_c, sk_rx, sk_ry = ell(SKIRT); ty_c, ty_rx, ty_ry = ell(TYPEK)
info = {'leds': [U(p) for p in LEDS], 'cap': [cap_c, cap_rx, cap_ry], 'skirt': [sk_c, sk_rx, sk_ry],
        'type': [ty_c, ty_rx, ty_ry], 'toggles': {k: U(v) for k, v in TOG.items()},
        'fs': U(FS['c']), 'fsRect': FS_RECT, 'glowRect': GLOW_RECT, 'bar': [U(BAR[:2]), U(BAR[2:])], 'row': [U(ROW[:2]), U(ROW[2:])]}
json.dump(info, open(os.path.join(HERE, 'layout.json'), 'w'), indent=1)
bx0, by0 = U(BAR[:2]); bx1, by1 = U(BAR[2:]); rx0, ry0 = U(ROW[:2]); rx1, ry1 = U(ROW[2:])
with open(os.path.join(ROOT, 'Layout.h'), 'w') as fo:
    fo.write('// genere par tools/make_background.py - ne pas modifier a la main (repere 760 x 958)\n#pragma once\n')
    fo.write('#include <juce_graphics/juce_graphics.h>\nnamespace layout\n{\n')
    fo.write('    // ellipses de perspective (face superieure vue par la photo) : centre, demi-axes\n')
    fo.write('    const juce::Point<float> capC %s, skirtC %s, typeC %s;\n' % (P2(cap_c), P2(sk_c), P2(ty_c)))
    fo.write('    const float capRx = %.2ff, capRy = %.2ff, skirtRx = %.2ff, skirtRy = %.2ff, typeRx = %.2ff, typeRy = %.2ff;\n' % (cap_rx, cap_ry, sk_rx, sk_ry, ty_rx, ty_ry))
    fo.write('    const juce::Point<float> capBaseC %s; const float capSideR = %.2ff;\n' % (P2(U(CAPBASE)), UL(CAP_SIDE_R)))
    fo.write('    const float unitPerPhotoPx = %.5ff;\n' % (S / 2))
    fo.write('    const juce::Point<float> momentaryPos %s, dryPos %s;\n' % (P2(U(TOG['momentary'])), P2(U(TOG['dry']))))
    fo.write('    const float toggleHalfW = %.2ff, toggleAbove = %.2ff, toggleBelow = %.2ff;\n' % (UL(TW), UL(TH0), UL(TH1)))
    fo.write('    const juce::Point<float> footPos %s; const float footR = %.2ff;\n' % (P2(U(FS['c'])), UL(FS['r'] + 6)))
    fo.write('    const juce::Rectangle<float> footRect { %.2ff, %.2ff, %.2ff, %.2ff };\n' % FS_RECT)
    fo.write('    const juce::Rectangle<float> glowRect { %.2ff, %.2ff, %.2ff, %.2ff };\n' % GLOW_RECT)
    fo.write('    const juce::Rectangle<float> rowRect { %.2ff, %.2ff, %.2ff, %.2ff };\n' % (rx0, ry0, rx1 - rx0, ry1 - ry0))
    fo.write('    const juce::Rectangle<float> barRect { %.2ff, %.2ff, %.2ff, %.2ff };\n}\n' % (bx0, by0, bx1 - bx0, by1 - by0))
Image.fromarray(np.clip(A, 0, 255).astype(np.uint8)).save(os.path.join(HERE, 'clean_photo_px.png'))
print(json.dumps(info))
