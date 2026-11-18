# Architecture commune : l'arbre, les feuilles, les mappings

Ce document fixe ce que toutes les applications Pixel Stereo ont en commun pour exposer et piloter leurs réglages. Il
sert de contrat : une application, une liste de paramètres, un menu ou un mapping se comportent de la même façon
partout. Les points marqués **à valider** attendent une réponse.

## Vocabulaire

| Mot | Sens | Dans `pxs::tree` |
|---|---|---|
| **Nœud** | un élément de l'arbre, avec un nom et un chemin (`/layer/1/opacity`) | `Node` |
| **Branche** | un nœud qui en contient d'autres, sans valeur ni action | `Kind::Container` |
| **Feuille** | un nœud qui ne contient rien et qui fait quelque chose | `Kind::Parameter`, `Message`, `Signal` |
| **Paramètre** | une feuille **avec un état** : on le lit, l'écrit, le sauvegarde ; il dit quand il change | `add(path, Parameter)`, ou rempli à la main |
| **Message** (action) | une feuille **sans état** : on la reçoit, elle déclenche quelque chose | `addMessage` |
| **Signal** (contrôleur) | une feuille **sans état** : l'application l'émet, personne ne l'écrit ; on ne mémorise pas sa valeur | `addSignal`, `fire` |

Une feuille n'a pas d'enfants : un nœud à qui on en donne devient une branche.
Un contrôleur matériel (MIDI, capteur, OSC entrant) est un **signal**.

## Le contrat : ce que chaque sorte de nœud fait, partout pareil

| | Branche | Paramètre | Message | Signal |
|---|---|---|---|---|
| **OSC reçu** | sans effet | écrit la valeur (dans ses limites) | exécute l'action | refusé |
| **OSC émis** | – | le retour (`changed`) vers qui a écrit et vers les destinations | – | le flux (`fire`) vers les destinations seulement |
| **OSCQuery** | `CONTENTS` | `TYPE`, `ACCESS` 1 à 3, `RANGE`, `VALUE` | `ACCESS` 2, `PXS_KIND: message`, pas de `VALUE` | `ACCESS` 1, `PXS_KIND: signal`, pas de `VALUE` |
| **Sauvegardé** | non | oui, par adresse (`parametersToJson`) sauf `transient` | non | non |
| **Réinitialiser** | – | oui, s'il a une valeur par défaut | – | – |
| **Verrouiller** | – | oui : aucune écriture (OSC, mapping, interface) | oui | – |
| **Source de mapping** | – | non (pour l'instant) | – | **oui** |
| **Cible de mapping** | – | **oui** | **oui** (déclenchée) | – |
| **Ligne dans le panneau** | un dossier qu'on plie | un champ selon le type : case, curseur, menu, texte | un bouton | un indicateur d'activité |
| **Menu contextuel** | – | Réinitialiser · Copier l'adresse OSC · Verrouiller · Animer · Lier à un signal | Copier l'adresse OSC · Verrouiller | Copier l'adresse OSC |

Le champ d'un paramètre vient de son type et de sa plage ; l'application ne le redessine pas.
Les messages restent des boutons que **l'application place elle-même** ; il n'y a pas de menu d'actions automatique.

## Mapping : l'Interaction de NyxMap, pour toutes les applications

Le mapping existe déjà dans NyxMap : c'est le panneau **Interaction** (son modèle est dans `Interactions.h`), avec le
MIDI learn. On ne réinvente rien : on **extrait** ce modèle dans `pxs`, puis chaque application, NyxMap comprise, l'utilise
depuis `pxs`.

Une **interaction** prend la valeur d'une **source** (clavier, MIDI, OSC, interne, audio ; un contrôleur est un signal),
la passe dans une **chaîne de filtres** (Remap, Moyenne, Attack Release, Damper, Courbe, Math, Seuil, Toggle,
Enveloppe…), applique le niveau maître, et l'envoie à la **cible**, qui la reçoit dans ses unités : un nombre, un
interrupteur (à partir de 0,5), un choix (par son indice), une action (déclenchée au passage de 0,5).
- **Reprise (pick-up) ou saut** quand la cible a été bougée par autre chose.
- **Mode Action** : des conditions sur la valeur de la source, et ce qui se passe quand elles deviennent vraies ou
  fausses.
- Couleurs et points XY : chaque canal a sa propre chaîne.
- Des **groupes** pour ranger la liste.
- Les signaux ne sont jamais mémorisés ; seule l'interaction l'est.

**Décisions prises**
- **Stockage** : `pxs` fournit le JSON (`Interaction::toJson` / `fromJson`) ; **l'application décide où il vit** (projet ou
  réglages).
- **Verrou** : il est **sauvegardé** avec ce que l'application sauvegarde. `pxs` fournit `lockedToJson` / `lockedFromJson`
  (la liste des adresses verrouillées).
- **Animer** : le drapeau `animatable` et la demande (le menu contextuel propose Animer ; l'application décide quoi en
  faire : la timeline de NyxMap, rien ailleurs).

## Ce qui existe dans NyxMap et passe dans `pxs`

Tes quatre idées sont en fait des choses que NyxMap a déjà. Le travail est une **extraction**, par étapes :

| Étape | Contenu | État |
|---|---|---|
| 1 | le modèle et le moteur de filtres des interactions (`Interactions.h`, `Anim.h`, graphe) : `pxs::interaction` | **fait** (tests : filtres, graphe) |
| 2 | l'entrée MIDI (`Midi.h`, RtMidi) et le MIDI learn | à faire |
| 3 | le panneau Interaction et le panneau de paramètres, rendus génériques sur l'arbre | à faire |
| 4 | les presets (snapshots) par adresse | à faire |
| 5 | le WebSocket `LISTEN` du serveur | à faire |
| 6 | NyxMap utilise les versions de `pxs` et supprime ses copies | à faire, en dernier |

Le filtre par signal de MpOsc (inversion, attaque, relâchement) devient une interaction de `pxs` (étape 3 pour son interface).

## Ce qui est déjà fait, et ce qui vient

| | État |
|---|---|
| arbre, trois sortes de feuilles, sortie unique, OSCQuery | fait (v0.13.0) |
| service réseau (UDP, HTTP, mDNS) | fait (v0.13.0) |
| liaison des widgets existants (`pxs::bind`) | fait (v0.13.0) |
| libellé, verrou, réinitialisation, observateurs | fait, à publier (v0.14.0) |
| modèle des interactions (extrait de NyxMap) | fait, à publier |
| MIDI, learn, panneaux, presets, `LISTEN` | voir le tableau des étapes |

## Autres idées, pas encore décidées

1. **Un drapeau « interne »** sur un nœud, pour ne pas exposer à l'extérieur ce qui ne sert qu'à l'interface.
2. **Limitation de débit** des signaux envoyés (par exemple 30 par seconde au plus par adresse).
3. **Annuler / rétablir** pour les modifications faites dans le panneau.
