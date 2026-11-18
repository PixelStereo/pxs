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

## Mapping : un signal pilote un paramètre

Un mapping relie une **source** (un signal) à une **cible** (un paramètre, ou un message).

| Champ | Rôle |
|---|---|
| `source`, `target` | adresses dans l'arbre |
| plage d'entrée → plage de sortie | la valeur du signal ramenée dans la plage de la cible |
| inversion, courbe | |
| lissage (attaque, relâchement, en secondes) | |
| seuil avec hystérésis | pour une cible booléenne, un choix ou un message : un message part sur le front montant |
| activé / désactivé | |

Règles :
- Les signaux ne sont **jamais** mémorisés ; seul le **lien** l'est : une liste de mappings par adresse, en JSON.
- Une cible **verrouillée** est ignorée. Un mapping dont une adresse n'existe plus **reste en sommeil** : il n'est pas
  supprimé, il se réveille si l'adresse revient.
- Reprise en douceur : tant que l'utilisateur tient la cible (`isEditing()`), le mapping ne la combat pas.
- Mode apprentissage : on arme une cible, on bouge une source, le lien est créé.
- Pas de boucle : un mapping ne se déclenche pas lui-même.

**À valider : où est sauvegardée la liste ?** Je propose : `pxs` fournit le JSON (`mappingsToJson` / `mappingsFromJson`) ;
**chaque application décide où il vit** (dans son projet, ou dans ses réglages). Rien n'est imposé par `pxs`.

## Animer

Le drapeau `animatable` d'un paramètre dit qu'une timeline ou un fondu peut le piloter ; le menu contextuel propose
**Animer** et le panneau émet une demande, que l'application traite.

**À valider : la portée.** La timeline n'existe que dans NyxMap. « Commun » veut dire :
1. le drapeau et la demande (chaque application décide quoi en faire) ; ou
2. en plus, un fondu commun d'une valeur à une autre sur une durée (déjà dans `Parameter::Ramp`) ; ou
3. une vraie timeline partagée (gros chantier).

## Ce qui est déjà fait, et ce qui vient

| | État |
|---|---|
| arbre, trois sortes de feuilles, sortie unique, OSCQuery | fait (v0.13.0) |
| service réseau (UDP, HTTP, mDNS) | fait (v0.13.0) |
| liaison des widgets existants (`pxs::bind`) | fait (v0.13.0) |
| libellé, verrou, réinitialisation, observateurs | fait, à publier (v0.14.0) |
| mappings signal vers paramètre | à écrire après validation |
| panneau commun de l'arbre (champs par type, menu contextuel, mappings) | à écrire après validation |

## Idées proposées, à trancher (aucune n'est construite)

1. **Le filtre par signal de MpOsc** (inversion, attaque, relâchement) devient un mapping de `pxs` : une seule logique de
   lissage dans toutes les applications.
2. **Presets et instantanés par adresse** dans les six applications : `parametersToJson` existe déjà, il manque l'interface.
3. **WebSocket `LISTEN`** dans le service réseau, pour que chaque application pousse ses valeurs en direct (aujourd'hui seulement NyxMap).
4. **Entrée MIDI comme source de signaux**, avec l'apprentissage ci-dessus.
5. **Un drapeau « interne »** sur un nœud, pour ne pas exposer à l'extérieur ce qui ne doit servir qu'à l'interface.
6. **Limitation de débit** des signaux envoyés (par exemple 30 par seconde au plus par adresse).
7. **Annuler / rétablir** pour les modifications faites dans le panneau.
8. **Le verrou est-il sauvegardé ?** Aujourd'hui c'est un état de session. **À valider.**
