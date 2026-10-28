# FreakOut (Studio Wiga)

Plugin VST3 (Windows et macOS) de **larsen naturel** pour guitare et basse, au format pédale simple,
d'après le fonctionnement de la DigiTech FreqOut (Natural Feedback Creator). Visuel original Studio Wiga.

## Commandes (comme la pédale d'origine)

| Commande | Rôle |
|---|---|
| **RANGE – GAIN** (chapeau central) | quantité de larsen (relative à l'attaque de la note), de rien à +6 dB |
| **RANGE – ONSET** (jupe moletée) | temps pour que le larsen atteigne sa pleine intensité : 30 ms à 6 s (12 h ≈ 0,4 s) |
| **TYPE** | SUB (octave en dessous), 1ST (unisson), 2ND, 3RD, 5TH (n × la fréquence de la note), NAT LOW, NAT HI |
| **MOMENTARY** | ON : le larsen n'agit que tant que le footswitch est enfoncé ; OFF : footswitch à accrochage |
| **DRY** | ON : la guitare reste ; OFF : larsen seul (gonflement façon EBow / thérémine) |
| **Échelle FEEDBACK** | LED centrale = effet actif ; les paires s'allument du centre vers l'extérieur pendant la montée du larsen |
| **Écran** | valeur du réglage touché (2,5 s), sinon l'harmonique qui larsène (ex. « H3 247 Hz B3 ») ; barre de presets |

NAT LOW n'utilise jamais la 5e harmonique ou au-dessus (pas de tierce majeure sur les accords mineurs) ;
NAT HI explore des partiels plus aigus et bascule plus volontiers d'un partiel à l'autre. Comme sur la pédale,
le choix dépend de l'attaque, du micro et de la tonalité : attaque forte + micro chevalet = larsen plus aigu.

## Moteur sonore (`FreakoutDSP.h`)

1. Détection de hauteur YIN sur signal décimé (~12 kHz), de 30 Hz (7 cordes en G grave, basse) à 1,6 kHz.
2. Choix de l'harmonique selon TYPE ; en NAT, score par partiel (énergie réelle dans la corde × courbe de
   résonance de la boucle × un peu de hasard par note) avec hystérésis, le centre de résonance descendant
   quand la note vieillit (les aigus s'éteignent d'abord, comme un vrai larsen).
3. Le partiel est isolé (passe-bande d'état), son signal analytique (paire de passe-tout en quadrature)
   pilote une PLL : l'oscillateur reste **en phase** avec la corde (vibrato, tirés, inharmonicité), puis
   « gonfle » de façon exponentielle (linéaire en dB, de −40 à +6 dB virtuels, saturation douce en haut) ;
   2e/3e harmoniques et micro-respiration apparaissent quand il sature.
4. Attaque de médiator = le larsen en cours est coupé et se reconstruit ; corde étouffée (> 18 dB en 80 ms)
   = il retombe en 70 ms ; legato / glissando = il glisse vers la nouvelle note sans clic.
5. Aucune latence, aucune allocation ni verrou dans le thread audio.

## MIDI

CC 20 Gain, CC 21 Onset (CC 52-53 en 14 bits), CC 22 Type, CC 23 Momentary, CC 24 Dry, CC 25 footswitch,
Program Change 1-13 = presets d'usine. Pédale d'expression : rampe sans saccade, course apprise automatiquement.
MOMENTARY ON : le footswitch MIDI agit tant qu'il est enfoncé.

## Fenêtre

Même taille par défaut qu'Echoflex (391 × 493), impossible de la réduire en dessous ; clic droit : taille
(défaut / 75 / 100 %), presets, sensibilité de déclenchement, mode du footswitch MIDI, table MIDI.

**Anti-gel v2** : rendu logiciel forcé sous Windows, aucun calcul lourd dans `paint()`, caches reconstruits
par le timer, chien de garde de redessin, aucun appel à l'hôte depuis le thread audio.

## Construction

JUCE 8.0.15 + CMake ; GitHub Actions (`windows-2022`, `macos-14`) produit les VST3 prêts à installer.
Visuel : `python3 tools/controls_art.py` puis `python3 tools/make_background.py` (génère aussi `Layout.h`).
Captures réelles : `tools/render` (compile la vraie fenêtre sous Linux).
Polices : Bebas Neue et Barlow Condensed (licence SIL OFL, `assets/fonts`).
