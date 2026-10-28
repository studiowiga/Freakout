# Kit graphique Studio Wiga (repris du visuel FreakOut)

| Fichier | Contenu | Usage |
|---|---|---|
| `switch_levier_ON.png` | inverseur à levier nickel, levier en haut, bague et ombre portée | position ON (haut) |
| `switch_levier_OFF.png` | même inverseur, levier en bas | position OFF (bas) |
| `switch_levier_milieu.png` | image intermédiaire (~55 ms pendant la bascule) | animation |
| `jack_out_gauche.png` | embase jack (écrou hexagonal) sortant de la tranche gauche | OUT, à gauche |
| `jack_in_droite.png` | embase jack sortant de la tranche droite | IN, à droite |

Photos réelles (lumière douce venant du haut, légèrement à gauche), 2 px par unité du repère 760 x 958,
fond transparent (canal alpha). Les inverseurs gardent leur ombre portée vers le bas à droite : ne jamais
les retourner en miroir (la lumière serait inversée). Source : `design/source/freakout_visuel.png`,
découpe par `tools/make_background.py`.
