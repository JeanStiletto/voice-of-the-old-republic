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
- Shift+Entrée — Ouvrir le menu d'actions unifié pour la cible actuelle (toutes les actions — attaquer, parler, pouvoirs de la Force, objets, capacités spéciales — dans un seul menu)
- Shift+1 … Shift+7 — Ouvrir une catégorie d'actions pour y choisir (1–3 sont les actions de la cible, 4–7 tes pouvoirs de la Force / objets / mines)
- H — Annoncer ta propre santé, tes effets actifs et ton arme équipée
- Ö (clavier allemand) / Accent grave (clavier US) — Lire le panneau Examiner pour la cible actuelle
- Shift+H — Ouvrir la file d'actions (vérifier ou vider les actions en file)
- Shift+L — Ouvrir le panneau de gain de niveau
- F1 — Ouvrir ou fermer la liste complète des touches ; Ctrl+F1 — lire les touches de l'écran actuel

<h3>Touches du mod — cycle des objets découverts</h3>

Un second cycle, par-dessus Q / E, qui parcourt les objets que tu as déjà découverts dans la zone actuelle — portes, conteneurs, personnages, transitions de zone, points de repère et tes propres marqueurs de carte — regroupés par catégorie. (Active « Cycle étendu » dans les Réglages du mod pour inclure aussi ce que tu n'as pas encore trouvé.)

- `,` / `.` — Objet précédent / suivant dans la catégorie actuelle
- Shift+`,` / Shift+`.` — Catégorie précédente / suivante (créatures, portes, conteneurs, transitions, marqueurs de carte, …)
- Ctrl+`,` / Ctrl+`.` — Sauter à l'objet le plus proche / le plus éloigné de la catégorie
- `/` (clavier US) ou `-` (clavier allemand) — Annoncer l'objet actuellement focalisé
- Shift+`/` (Shift+`-`) — Marche automatique vers cet objet
- Ctrl+`/` (Ctrl+`-`) — Armer une balise audio qui pingue le chemin à mesure que tu te déplaces

<h3>Touches du mod — orientation et équipe</h3>

- AltGr (Alt droit, seul) — Annoncer l'orientation actuelle en direction cardinale
- N — Tourner la caméra de 90° dans le sens horaire vers la direction cardinale suivante ; si une balise est armée, viser le prochain point de passage de la balise à la place
- Tab — Annonce le nouveau chef d'équipe après que le moteur a basculé le contrôle

<h3>Touches du mod — mode vue</h3>

Appuie sur B pour entrer en mode vue. Tant que le mode vue est actif :

- A / D — Faire pivoter la caméra sans déplacer le personnage
- Entrée — Interagir avec ce que la caméra pointe, ou marche automatique vers ce point
- Shift+Entrée — Ouvrir le menu d'actions sur la cible de la caméra
- B à nouveau — Quitter le mode vue

<h3>Touches du mod — écran de carte</h3>

Lorsque la carte du jeu est ouverte :

- Flèches / Haut / Bas — Parcourir les notes et points de repère de la carte
- `,` / `.` — Parcourir les marqueurs de carte (même vocabulaire que le cycle des objets découverts)
- Shift+N — Poser un marqueur de carte personnel à la position monde actuelle du curseur (nommé automatiquement d'après la salle ou le repère le plus proche). Le nouveau marqueur rejoint le cycle immédiatement et Ctrl+`-` y posera une balise

<h3>Touches du mod — sous-menus</h3>

Lorsqu'un sous-menu du mod est ouvert (le menu d'actions unifié, une catégorie d'actions, la file d'actions) :

- Haut / Bas — Déplacer le focus
- Gauche / Droite — Passer d'une colonne ou variante à l'autre
- Entrée — Activer la ligne focalisée
- Shift+Entrée — (file d'actions uniquement) Vider toutes les actions en file
- Esc — Fermer le sous-menu

<h3>Touches du mod — spécifiques au contexte</h3>

- Q ou E dans un panneau Conteneur — Tout prendre / donner des objets
- Q ou E dans un panneau Boutique — Basculer entre Acheter et Vendre

Dans le champ de nom de création de personnage (et autres champs de saisie de texte) :

- Haut / Bas — Relire le texte actuel depuis le début
- Entrée — Valider
- Esc — Annuler

<h2>Systèmes de navigation en un coup d'œil</h2>

KOTOR est un jeu de rôle en 3D, tu passes donc l'essentiel de ton temps à te déplacer à travers des salles et autour d'objets. Le mod superpose quelques systèmes pour te garder orienté — chacun s'annonce de lui-même à l'usage.

<h3>Cycle de cibles — Q / E</h3>

Ton principal moyen de trouver des choses et d'agir dessus. Q / E parcourent les créatures, portes et objets utilisables que la caméra peut voir ; ce qui est ciblé est ce sur quoi Entrée et les touches d'action 1–7 agissent. Le mod annonce chaque nouvelle cible.

<h3>Cycle des objets découverts — `,` / `.`</h3>

Pour retrouver les choses que tu as déjà découvertes. `,` / `.` parcourent chaque objet que tu as découvert dans la zone actuelle — portes, conteneurs, personnages, transitions, points de repère, tes propres marqueurs — regroupés par catégorie. Annonce-en un, marche automatiquement jusqu'à lui, ou arme une balise audio. (Réglages du mod → « Cycle étendu » l'élargit pour inclure ce que tu n'as pas encore trouvé.)

<h3>Menu d'actions unifié — Shift+Entrée</h3>

Un seul menu contenant toutes les actions pour la cible actuelle — attaquer, parler, pouvoirs de la Force, objets, capacités spéciales. Les flèches déplacent le focus, Entrée active. Il remplace les menus radial, de cible et personnel séparés du jeu.

<h3>Carte — M</h3>

La carte intégrée à KOTOR, rendue navigable. Déplace le curseur avec les flèches pour lire le terrain et les marqueurs, ou parcours les marqueurs de la carte avec `,` / `.` dans le même vocabulaire que dans le monde. Le brouillard de guerre est respecté, et Shift+N pose un marqueur personnel au curseur.

<h3>Repères de murs et descriptions de forme des salles</h3>

À mesure que tu te déplaces, une couche audio 3D continue joue de doux clics positionnels depuis les murs les plus proches — les murs plus proches sonnent plus fort — pour que tu gardes un sens constant de l'espace autour de toi. Et entrer dans une salle en prononce le nom, la forme (couloir, jonction, cul-de-sac, espace ouvert) et les sorties visibles, le tout calculé en direct à partir du walkmesh du jeu.

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
