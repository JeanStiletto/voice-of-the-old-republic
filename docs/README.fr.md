---
layout: default
title: Voice of the Old Republic — Français
permalink: /docs/README.fr.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <strong>Français</strong> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a></p>

<h2>Qu'est-ce que ce mod</h2>

**Voice of the Old Republic** est un mod de lecture d'écran et de navigation au clavier pour **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, version Steam) qui permet aux joueurs entièrement aveugles de jouer à KOTOR 1 avec n'importe quel lecteur d'écran moderne. La synthèse vocale passe par le pont vocal Prism, qui prend en charge tous les lecteurs d'écran majeurs sur toutes les plateformes majeures.

Le mod est écrit par un développeur aveugle. Chaque flux de travail — installation, jeu, contribution — est conçu pour être réalisable avec uniquement un lecteur d'écran et un clavier.

<h2>Prérequis</h2>

- Windows 10 ou plus récent
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam ou GoG ; les deux sont identiques au niveau des octets pour notre usage)
- Un lecteur d'écran. La synthèse vocale passe par Prism, qui prend en charge l'ensemble complet des lecteurs d'écran activement utilisés ; si ton lecteur d'écran fonctionne avec tout le reste sur ton système, il fonctionnera avec ce mod
- Environ 200 Mo d'espace disque libre pour le runtime du patcher, le K1 Community Patch et le runtime vocal embarqué

<h3>Versions du jeu non prises en charge dans cette version</h3>

- Portages Aspyr mobile / macOS (binaire différent)
- Exécutables déjà patchés (modifiés par UniWS, modifiés par KOTOR High-Resolution Menus)
- Builds dont le SHA-256 de `swkotor.exe` ne correspond pas aux hachages Steam ou GoG 1.0.3 reconnus

Si l'installeur signale une incompatibilité de version, ouvre une issue avec le hachage affiché. La base d'adresses couvre Steam et GoG d'emblée, et ajouter un nouveau repack identique au niveau des octets demande généralement une seule ligne dans le manifeste.

<h2>Installation</h2>

1. Télécharge `VoiceOfTheOldRepublicInstaller.exe` depuis la dernière release sur GitHub
2. Ferme KOTOR s'il est en cours d'exécution
3. Clic droit sur l'installeur et choisis **Exécuter en tant qu'administrateur**. Au premier lancement, Windows SmartScreen avertira d'un « Éditeur inconnu » — clique sur **Informations complémentaires → Exécuter quand même**. L'installeur n'est pas encore signé numériquement, cet avertissement est donc attendu
4. (Recommandé) Sauvegarde ton dossier de sauvegardes situé dans `%USERPROFILE%\Documents\Swkotor\saves\` avant l'installation si tu as une partie en cours
5. Parcours les écrans de l'installeur. Il détecte ton installation de KOTOR, installe le framework de patch, déploie le mod et (par défaut) inclut le K1 Community Patch ainsi que les correctifs widescreen / menus haute résolution
6. Lance le jeu depuis l'écran final de l'installeur ou depuis Steam

Pour désinstaller, relance l'installeur et choisis l'option de désinstallation, ou utilise « Programmes et fonctionnalités ». Le désinstalleur supprime uniquement les fichiers de ce mod — K1CP et tout autre mod optionnel que tu as choisi à l'installation restent en place. Pour revenir à un KOTOR entièrement vanille, utilise « Vérifier l'intégrité des fichiers du jeu » de Steam ou réinstalle depuis GoG après la désinstallation.

<h2>Raccourcis clavier</h2>

Le mod conserve la table des touches par défaut du jeu. Tout ce qui n'est pas listé ci-dessous se comporte comme dans le jeu non modifié. Chaque action ci-dessous peut être réassignée dans `swkotor.ini` (touches du jeu) ou, pour les touches ajoutées par le mod, sera réassignable depuis un écran de paramètres dans le jeu dans une version ultérieure.

<h3>Touches du jeu que tu utiliseras le plus</h3>

- W / S — Avancer / reculer
- A / D — Faire pivoter la caméra à gauche / à droite
- Z / C — Pas chassés à gauche / à droite
- Q / E — Cibler à gauche / à droite
- R — Action par défaut sur la cible actuelle (attaquer, parler, ouvrir)
- 1 / 2 / 3 — Utiliser les trois actions du menu d'actions de la cible actuelle
- 4 / 5 / 6 / 7 — Utiliser les emplacements pouvoir de la Force / medpac / objet / mine du joueur
- Tab — Changer de chef d'équipe
- F — Annuler le combat, G — Furtivité, V — Mode solo, X — Fioriture d'arme
- Barre d'espace — Pause
- Esc — Menu du jeu
- F4 — Sauvegarde rapide, F5 — Chargement rapide
- I — Inventaire de l'équipe, U — Équipement, P — Fiche de personnage, K — Compétences / dons / pouvoirs
- M — Carte, L — Quêtes, J — Messages, O — Options
- Souris 1 — Cliquer dans le monde 3D (rarement nécessaire ; voir le mode vue ci-dessous)

<h3>Touches du mod — interaction avec le monde</h3>

- Entrée — Déclencher l'action par défaut sur la cible actuellement annoncée (équivaut à un clic de Souris 1 dans le monde)
- Shift+Entrée — Ouvrir le menu d'actions radial sur la cible actuelle
- Ö — Lire le panneau Examiner pour la cible actuelle (bascule)
- Shift+L — Ouvrir le panneau de gain de niveau
- Shift+H — Ouvrir le sous-menu de file de combat (vérifier ou vider les actions en file)

<h3>Touches du mod — cycle dans le monde</h3>

Un cycle de cibles parallèle qui couvre aussi les portes, conteneurs, transitions de zone et marqueurs de carte — des choses que le Q / E du jeu ne récupère pas.

- `,` / `.` — Élément précédent / suivant dans la catégorie actuelle
- Shift+`,` / Shift+`.` — Catégorie précédente / suivante (créatures, portes, conteneurs, transitions, marqueurs de carte, …)
- `-` (clavier allemand) ou `/` (clavier US) — Annoncer la cible de cycle actuellement focalisée
- Shift+`-` — Marche automatique vers cette cible
- Ctrl+`-` — Armer une balise audio qui pingue le chemin vers la cible à mesure que tu te déplaces

<h3>Touches du mod — orientation et équipe</h3>

- AltGr (Alt droit, seul) — Annoncer l'orientation actuelle en direction cardinale
- N — Tourner la caméra de 90° dans le sens horaire vers la direction cardinale suivante ; si une balise est armée, viser le prochain point de passage de la balise à la place
- Tab — Annonce le nouveau chef d'équipe après que le moteur a basculé le contrôle

<h3>Touches du mod — mode vue</h3>

Appuie sur B pour entrer en mode vue. Tant que le mode vue est actif :

- A / D — Faire pivoter la caméra sans déplacer le personnage
- Entrée — Interagir avec ce que la caméra pointe, ou marche automatique vers ce point
- Shift+Entrée — Forcer l'ouverture du radial sur la cible de la caméra
- B à nouveau — Quitter le mode vue

<h3>Touches du mod — écran de carte</h3>

Lorsque la carte du jeu est ouverte :

- Flèches / Haut / Bas — Parcourir les notes et points de repère de la carte
- `,` / `.` — Parcourir les marqueurs de carte (même vocabulaire que le cycle dans le monde)
- Shift+N — Poser un marqueur de carte personnel à la position monde actuelle du curseur (nommé automatiquement d'après la salle ou le repère le plus proche). Le nouveau marqueur rejoint le cycle immédiatement et Ctrl+`-` y posera une balise

<h3>Touches du mod — sous-menus</h3>

Lorsqu'un sous-menu du mod est ouvert (sous-menu de la barre d'action, sous-menu de file de combat, menu radial) :

- Haut / Bas — Déplacer le focus
- Gauche / Droite — Déplacer le focus dans la grille 4×3 du radial
- Entrée — Activer la ligne focalisée
- Shift+Entrée — (file de combat uniquement) Vider toutes les actions en file
- Esc — Fermer le sous-menu

<h3>Touches du mod — spécifiques au contexte</h3>

- Q ou E dans un panneau Conteneur — Tout prendre / donner des objets
- Q ou E dans un panneau Boutique — Basculer entre Acheter et Vendre

Dans le champ de nom de création de personnage (et autres champs de saisie de texte) :

- Haut / Bas — Relire le texte actuel depuis le début
- Entrée — Valider
- Esc — Annuler

<h2>Systèmes de navigation en un coup d'œil</h2>

KOTOR est un jeu de rôle en 3D, donc la majeure partie du temps de jeu est passée à déplacer un personnage à travers des salles et autour d'objets. Le mod te donne plusieurs façons de naviguer, superposées les unes aux autres.

<h3>Cycle de cibles (Q / E)</h3>

Le cycle de cibles intégré au jeu. Récupère les créatures, portes et placeables utilisables à environ 30 mètres que la caméra peut voir. Q va à gauche, E va à droite. Ce qui est ciblé est ce sur quoi R, 1, 2, 3 et la barre d'actions agissent. Le mod annonce chaque nouvelle cible.

<h3>Cycle dans le monde (`,` / `.`)</h3>

Un second cycle que le mod ajoute par-dessus Q / E. Couvre ce que Q / E manque — transitions de zone, points de passage, tes propres marqueurs de carte — et regroupe tout par catégorie pour que tu puisses parcourir un type de chose à la fois. Utilise `-` pour annoncer, Shift+`-` pour la marche automatique, Ctrl+`-` pour armer une balise audio.

<h3>Descriptions de la forme des salles</h3>

Lorsque tu entres dans une nouvelle salle, le mod prononce le nom de la zone, la forme de la salle (couloir, jonction, cul-de-sac, espace ouvert) et les sorties visibles depuis ta position actuelle. La forme est calculée en direct à partir du walkmesh du jeu, pas à partir d'une description rédigée à la main, donc elle reste exacte dans toutes les zones du jeu.

<h3>Repères audio de distance aux murs</h3>

Une couche audio 3D continue joue de doux clics positionnels aux murs les plus proches autour de toi. Plus le mur est proche, plus le repère est fort ; plus tu es loin, plus il est faible. À mesure que tu te déplaces, les repères changent de hauteur, de volume et de direction, te donnant un sens spatial constant de l'emplacement des murs sans avoir à interroger. C'est la principale fonctionnalité « voir la salle » du mod et la plus utile dans les espaces intérieurs étroits.

<h3>Mode vue (B)</h3>

Un mode « regarder sans marcher ». Appuie sur B et ton mouvement W / S est gelé ; A / D fait maintenant pivoter librement la caméra sans tourner ton personnage. De là, tu peux pointer la caméra sur un objet distant, appuyer sur Entrée pour la marche automatique vers lui, ou Shift+Entrée pour ouvrir le radial dessus. Utile pour les placeables distants qui ne sont pas dans le cycle Q / E ou `,` / `.`, et pour examiner une grande salle.

<h3>Mode carte (M)</h3>

La carte intégrée à KOTOR. Le mod rend chaque marqueur de carte (portes, transitions, marqueurs de quête, tes propres marqueurs Shift+N) parcourable avec `,` / `.` et annoncé avec le même vocabulaire utilisé dans le monde. Le brouillard de guerre est respecté — les marqueurs inexplorés restent cachés jusqu'à ce que tu les aies vus. Shift+N pose un marqueur personnel au curseur qui persiste jusqu'à la fermeture du panneau et rejoint le cycle immédiatement.

<h2>Signaler des bugs</h2>

L'écran post-installation de l'installeur dispose d'un bouton **Collecter les logs** qui zippe le log de patch le plus récent et tout dump Windows Error Reporting dans ton dossier Téléchargements. Joins ce zip à une [issue GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) et décris ce que tu étais en train de faire. Si tu peux reproduire un crash, mentionne dans quelle zone tu étais — l'annonce de salle ou de zone l'aura dite juste avant.

<h2>Problèmes connus</h2>

Pour le backlog actuel des bugs, fonctionnalités prévues et points rugueux, voir [docs/known-issues.md](known-issues.md).

<h2>Contribuer</h2>

Les contributions sont les bienvenues — en particulier les correctifs pour les langues, configurations système ou lecteurs d'écran que le développeur ne peut pas tester localement. Avant de commencer, parcours le fichier des problèmes connus ci-dessus pour voir si ton idée est déjà dans le backlog.

- Guide de contribution : [CONTRIBUTING.md](../CONTRIBUTING.md)
- Vue d'ensemble de l'architecture : [ARCHITECTURE.md](../ARCHITECTURE.md)

<h2>Usage de l'IA</h2>

Le code du mod est écrit avec une assistance importante de Claude d'Anthropic (série Opus). Dans une industrie du jeu vidéo qui a historiquement refusé de livrer une accessibilité native pour des titres comme KOTOR, le modding assisté par IA est ce qui rend un projet de cette taille réalisable pour un seul développeur aveugle. Chaque changement est relu et testé en jeu par l'auteur avant d'être livré.

<h2>Licence</h2>

Le code source du mod est sous licence GNU General Public License v3 (voir [LICENSE](../LICENSE)). Les dépendances intégrées sous `third_party/` conservent leurs propres licences (Prism est MPL-2.0 ; Tolk est LGPL ; Kotor-Patch-Manager est intégré selon les termes de son upstream ; dsoal et OpenAL Soft, lorsque le chemin spatial-audio optionnel est activé, sont LGPL-2.1). Le jeu lui-même et les fichiers de données de BioWare ne sont pas redistribués par ce projet.

<h2>Crédits</h2>

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), la base de données Ghidra issue de la rétro-ingénierie, et le framework de patch sur lequel ce mod fonctionne
- **Prism** (Ethin P.) — pont vocal multiplateforme couvrant tous les lecteurs d'écran majeurs, avec repli SAPI
- L'équipe du **K1 Community Patch** (KOTORCommunityPatches) — couche de correctifs incluse
- **xoreos / xoreos-tools** — réimplémentation open-source du moteur ; référence croisée pour les formats de fichiers
- La communauté **DeadlyStream** — base de connaissances du modding
- **Claude (Anthropic)** — partenaire de pair-programming à travers les générations Opus 4.5, 4.6 et 4.7
