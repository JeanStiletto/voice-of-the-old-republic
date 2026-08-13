---
layout: default
title: Voice of the Old Republic — Français
permalink: /docs/README.fr.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <strong>Français</strong> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a> · <a href="/voice-of-the-old-republic/docs/README.pl.html">Polski</a> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>Qu'est-ce que ce mod</h2>

**Voice of the Old Republic** est un projet visant à rendre les jeux Star Wars: Knights of the Old Republic accessibles aux joueurs totalement aveugles. Il utilise le pont vocal Prism pour prendre en charge tous les lecteurs d'écran, et ajoute des aides à la navigation, l'accessibilité des menus et des repères sonores spécifiques qui rendent les mini-jeux accessibles.

En l'état actuel, Knights of the Old Republic I est entièrement jouable et terminable, avec tous les mini-jeux, toutes les quêtes et tous les styles de jeu. Il se joue au clavier ou avec une manette de type Xbox — le mod embarque sa propre prise en charge des manettes (voir la section manette plus bas) ; seuls les mini-jeux demandent encore le clavier.

Knights of the Old Republic II est porté : l’installateur configure aussi le mod pour lui, et le jeu parle. Il n’a cependant pas encore été terminé, et quelques blocages devront être levés avant que ce soit possible — ses mini-jeux et ses messages de tutoriel ne sont pas encore couverts.

Le mod est traduit dans toutes les langues prises en charge — anglais, allemand, français, italien, espagnol — et prend également en charge une traduction polonaise et une traduction russe. Pour Knights of the Old Republic I, il prend en charge les versions Steam et GoG 1.03 ainsi que la version de 2004 utilisée par les traductions polonaise et russe. Pour Knights of the Old Republic II, il ne prend en charge que la version d’Aspyr de 2015 — celle que Steam et GoG distribuent aujourd’hui.

<h2>Que sont les jeux Knights of the Old Republic</h2>

Knights of the Old Republic est une paire de jeux de rôle Star Wars portés par leur histoire, situés environ quatre mille ans avant les films. Le premier a été réalisé par BioWare en 2003 ; le second — Knights of the Old Republic II: The Sith Lords — par Obsidian en 2004, et il se déroule quelques années après la partie I. Les deux tournent sur le même moteur, ce qui permet au mod de passer de l'un à l'autre.

Dans les deux jeux, vous créez votre propre personnage, réunissez un groupe de compagnons, voyagez entre les planètes et façonnez l'histoire par vos choix de dialogue et selon que vous suivez le côté lumineux ou le côté obscur de la Force.

Le combat fonctionne de la même manière dans les deux : en temps réel avec pause, bâti sur les règles Star Wars d20 du jeu de rôle sur table — vous mettez des actions en file d'attente et les dés les résolvent. Chaque round dure 6 secondes réelles et vous pouvez faire pause à tout moment pour examiner le champ de bataille et mettre en file d'attente des actions spéciales bien plus puissantes que les attaques automatiques du personnage. Vous pouvez mettre jusqu'à 4 actions en file d'attente par personnage avant de devoir laisser le combat se dérouler, et vous pouvez annuler certaines actions si les événements du combat vous obligent à changer de stratégie.

<h2>Prérequis</h2>

- Windows 10 ou plus récent
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam ou GoG ; les deux sont identiques octet par octet pour nos besoins)
- Un lecteur d'écran. La parole passe par Prism, qui prend en charge l'ensemble des lecteurs d'écran en usage actif ; si votre lecteur d'écran fonctionne avec le reste de votre système, il fonctionnera avec ce mod
- Environ 200 Mo d'espace disque libre pour l'exécution du patcher, le K1 Community Patch et le moteur vocal fourni

<h3>Versions du jeu non prises en charge dans cette version</h3>

- Portages Aspyr mobile / macOS (binaire différent)
- Exécutables déjà patchés (modifiés par UniWS, par KOTOR High-Resolution Menus)
- Builds dont le SHA-256 de `swkotor.exe` ne correspond pas aux empreintes Steam ou GoG 1.0.3 reconnues

Si l'installeur signale une différence de version, ouvrez un ticket en indiquant l'empreinte affichée. La base d'adresses couvre Steam et GoG d'origine, et ajouter un nouveau repack identique octet par octet ne demande généralement qu'une ligne de manifeste.

<h2>Installation</h2>

1. Téléchargez `VoiceOfTheOldRepublicInstaller.exe` depuis la dernière version publiée sur GitHub
2. Fermez KOTOR s'il est en cours d'exécution
3. Faites un clic droit sur l'installeur et choisissez **Exécuter en tant qu'administrateur**. Au premier lancement, Windows SmartScreen avertira d'un « éditeur inconnu » — cliquez sur **Informations complémentaires → Exécuter quand même**. L'installeur n'est pas encore signé, cet avertissement est donc normal (voir la section Dépannage pour vérifier le téléchargement)
4. (Recommandé) Sauvegardez votre dossier de sauvegardes dans `%USERPROFILE%\Documents\Swkotor\saves\` avant d'installer si vous avez une partie en cours
5. Parcourez les écrans de l'installeur. Il détectera votre installation de KOTOR, installera le framework de patch, déploiera le mod et, par défaut, intégrera le K1 Community Patch ainsi que les correctifs écran large / menus haute résolution
6. Lancez le jeu depuis le dernier écran de l'installeur ou depuis Steam

<h2>Désinstallation</h2>

Relancez l'installeur et choisissez l'option de désinstallation, ou utilisez « Ajout/Suppression de programmes ». Le désinstalleur ne supprime que les fichiers de ce mod — K1CP et les autres mods optionnels choisis à l'installation restent en place. Pour revenir à un KOTOR entièrement d'origine, utilisez « Vérifier l'intégrité des fichiers du jeu » de Steam ou réinstallez depuis GoG après la désinstallation.

<h2>Premiers pas</h2>

Quand vous démarrez une nouvelle partie, KOTOR vous guide d'abord dans la **création de personnage** : vous choisissez une classe (Soldat, Eclaireur ou Voyou), un portrait, et vous réglez vos caractéristiques, compétences et dons. Le mod lit chaque panneau à mesure que vous le parcourez — prenez votre temps, rien n'est chronométré.

Vous vous réveillez ensuite sur l'**Endar Spire**, un vaisseau de la République attaqué. C'est la zone tutoriel du jeu. Le mod remplace les fenêtres de tutoriel affichées à l'écran par des indications clavier écrites pour les utilisateurs de lecteur d'écran, si bien que vous apprenez les commandes en chemin. Suivez Trask, votre guide, vers les capsules de sauvetage.

Quelques habitudes qui rendent le début de partie bien plus facile :

- **Trouvez les choses avec Q / E**, et revenez à ce que vous avez déjà trouvé avec le cycle `,` / `.` (voir les raccourcis clavier plus bas).
- **Appuyez sur H** à tout moment pour entendre votre santé et votre état, et sur **F1** pour la liste complète des touches.
- **Écoutez la pièce.** Entrer dans une pièce en annonce le nom, la forme et les sorties, et une légère couche audio vous tient informé des murs les plus proches pendant que vous vous déplacez.
- **Regardez les réglages dès le début.** Appuyez sur O pour les Options du jeu. Sous Gameplay se trouvent les options de pause automatique, qui décident quand le combat s'arrête de lui-même — cela vaut la peine de les régler avant votre premier vrai combat — ainsi que l'assignation des touches.
- **Et tout en bas de la liste des Options : les Paramètres du mod.** Presque tout ce que ce mod ajoute peut y être ajusté, activé ou désactivé, les touches du mod peuvent y être réassignées, et le glossaire audio joue chaque repère sonore avec son nom pour que vous appreniez ce que chacun signifie.

Après l'Endar Spire, vous atteignez **Taris**, le premier grand monde, et l'histoire s'ouvre à partir de là.

<h2>Raccourcis clavier</h2>

Le mod conserve la disposition des touches par défaut du jeu, avec une modification ergonomique appliquée par l'installeur lors d'une nouvelle installation (voir la note sur le pas chassé / la rotation de caméra ci-dessous). Tout ce qui n'est pas listé ici se comporte comme dans le jeu non modifié. Les touches du jeu se réassignent dans Options → Gameplay → Assignation des touches, ou directement dans `swkotor.ini`. Les touches ajoutées par le mod se réassignent dans Paramètres du mod → Raccourcis clavier.

<h3>Touches du jeu que vous utiliserez le plus</h3>

- W / S — Avancer / reculer
- A / D — Pas chassé à gauche / à droite
- Z / C — Tourner la caméra à gauche / à droite (**Y / C** sur un clavier allemand QWERTZ)
- Q / E — Changer de cible vers la gauche / la droite
- R — Action par défaut sur la cible actuelle (attaquer, parler, ouvrir)
- 1 / 2 / 3 — Utiliser les trois actions du menu d'actions de la cible
- 4 / 5 / 6 / 7 — Utiliser les emplacements pouvoir de Force / medpac / objet / mine du joueur
- Tab — Changer de chef de groupe
- F — Annuler le combat, G — Discrétion, V — Mode solo, X — Faire tournoyer l'arme
- Barre d'espace — Pause
- Échap — Menu du jeu
- F4 — Sauvegarde rapide, F5 — Chargement rapide (dans le menu principal, F5 recherche plutôt une mise à jour du mod)
- I — Inventaire du groupe, U — Équipement, P — Fiche de personnage, K — Compétences / dons / pouvoirs
- M — Carte, L — Quêtes, J — Messages, O — Options
- Souris 1 — Clic dans le monde en 3D (rarement nécessaire ; voir le mode vue ci-dessous)

> **Pas chassé / rotation de caméra :** les valeurs par défaut de KOTOR sont Z / C pour le pas chassé et A / D pour tourner la caméra. L'installeur d'accessibilité les échange lors d'une installation complète neuve afin que les deux forment un groupe confortable sur la rangée du bas — **pas chassé sur A / D, rotation de caméra sur Z / C**. C'est la disposition listée ci-dessus. Si vous avez mis à jour une installation existante, ou si vous réassignez vous-même les touches dans `swkotor.ini`, vos touches peuvent différer.

<h3>Touches du mod — interaction avec le monde</h3>

- Entrée — Déclencher l'action par défaut sur la cible actuellement annoncée (équivalent d'un clic Souris 1 dans le monde)
- Maj+Entrée — Ouvrir le menu d'actions unifié pour la cible actuelle (toutes les actions — attaquer, parler, pouvoirs de Force, objets, capacités spéciales — dans un seul menu)
- Maj+1 … Maj+7 — Ouvrir une catégorie d'actions pour y choisir (1–3 sont les actions de la cible, 4–7 vos pouvoirs de Force / objets / mines)
- H — Annoncer votre santé, vos effets actifs et votre arme équipée
- Maj+H — Ouvrir la file d'actions (consulter ou vider les actions en attente)
- Maj+L — Ouvrir l'écran de montée de niveau
- F1 — Ouvrir ou fermer la liste complète des touches ; Ctrl+F1 — lire les touches de l'écran actuel
- Ctrl+R — Copier le dernier texte lu dans le presse-papiers (une ligne de dialogue, une entrée du journal, le nom d'un PNJ — ce qui vient d'être lu)

<h3>Touches du mod — cycle des objets découverts</h3>

Un second cycle, en plus de Q / E, qui parcourt les objets que vous avez déjà découverts dans la zone actuelle — portes, conteneurs, personnages, transitions de zone, points de repère et vos propres marqueurs de carte — regroupés par catégorie. (Activez « Sélection d'objets sur toute la carte » dans les Paramètres du mod pour inclure aussi ce que vous n'avez pas encore trouvé.)

- `,` / `.` — Objet précédent / suivant dans la catégorie actuelle
- Maj+`,` / Maj+`.` — Catégorie précédente / suivante (créatures, portes, conteneurs, transitions, marqueurs de carte, …)
- Ctrl+`,` / Ctrl+`.` — Aller à l'objet le plus proche / le plus éloigné de la catégorie
- `/` (disposition US) ou `-` (disposition allemande) — Annoncer l'objet actuellement ciblé
- Maj+`/` (Maj+`-`) — Marcher automatiquement jusqu'à cet objet
- Ctrl+`/` (Ctrl+`-`) — Armer une balise audio qui sonne le chemin pendant que vous avancez

<h3>Touches du mod — orientation et groupe</h3>

- AltGr (Alt droite, seule) — Annoncer l'orientation actuelle comme point cardinal
- N — Tourner la caméra de 90° dans le sens horaire vers le point cardinal suivant ; si une balise est armée, viser plutôt le point de passage suivant de la balise
- Tab — Annonce le nouveau chef de groupe après que le moteur a changé de contrôle

<h3>Touches du mod — mode vue</h3>

Appuyez sur B pour entrer en mode vue. Tant que le mode vue est actif :

- A / D — Faire pivoter la caméra sans déplacer le personnage
- Entrée — Interagir avec ce que la caméra vise, ou marcher automatiquement jusqu'à ce point
- Maj+Entrée — Ouvrir le menu d'actions sur la cible de la caméra
- B à nouveau — Quitter le mode vue

<h3>Touches du mod — écran de la carte</h3>

Quand la carte du jeu est ouverte :

- Flèches / Haut / Bas — Parcourir les notes et les points de repère de la carte
- `,` / `.` — Parcourir les marqueurs de carte (même vocabulaire que le cycle des objets découverts)
- Maj+N — Poser un marqueur personnel à la position actuelle du curseur dans le monde (nommé automatiquement d'après la pièce ou le repère le plus proche). Le nouveau marqueur rejoint le cycle immédiatement et Ctrl+`-` y pose une balise

<h3>Touches du mod — sous-menus</h3>

Quand un sous-menu du mod est ouvert (le menu d'actions unifié, un menu de catégorie, la file d'actions) :

- Haut / Bas — Déplacer le focus
- Gauche / Droite — Passer d'une colonne ou d'une variante à l'autre
- Entrée — Activer la ligne ciblée
- Maj+Entrée — (file d'actions uniquement) Vider toutes les actions en attente
- Échap — Fermer le sous-menu

<h3>Touches du mod — spécifiques au contexte</h3>

- Q ou E dans un panneau Conteneur — Tout prendre / donner des objets
- Q ou E dans un panneau Boutique — Basculer entre Acheter et Vendre

Dans le champ du nom à la création de personnage (et les autres champs de saisie de texte) :

- Haut / Bas — Relire le texte actuel depuis le début
- Entrée — Valider
- Échap — Annuler

<h3>Touches du mod — mini-jeu Pazaak</h3>

Quand le plateau de Pazaak est ouvert :

- Haut / Bas — Passer d'une zone à l'autre : votre main, votre table, la table de l'adversaire, les actions (Rester / Finir le tour)
- Gauche / Droite — Se déplacer dans la zone actuelle (les emplacements de main vides sont ignorés)
- Entrée — Jouer la carte de main ciblée, ou activer l'action ciblée
- S — Rester
- E — Finir le tour
- C — Lire votre main
- T — Lire les deux tables avec leurs totaux
- Maj+C — Combien de cartes l'adversaire a encore en main
- Carte plus/moins — Entrée ouvre un choix de signe ; Gauche / Droite choisissent plus ou moins, Entrée joue avec ce signe, Échap annule

Sur l'écran de mise avant la partie, la première entrée lit votre mise actuelle, le maximum de la table et vos crédits ; allez sur « Diminuer la mise » / « Augmenter la mise » et appuyez sur Entrée pour changer la mise, puis sur le bouton de mise du jeu pour la placer. L'éditeur de deck annexe lit chaque carte et chaque emplacement de deck.

<h2>Manette</h2>

Le mod embarque sa propre prise en charge des manettes — KOTOR 1 n'a aucun code manette à lui, c'est donc le mod qui pilote directement la manette. Toute manette XInput à disposition Xbox fonctionne ; branchez-la avant de lancer le jeu. Le clavier et la manette restent actifs côte à côte, et chaque action de jeu déclenchée par la manette passe par vos propres associations de touches — vos réassignations sont donc respectées. Avec une manette branchée, la liste des touches (F1) gagne une section Manette ; sans manette, cette section disparaît. Les mini-jeux (Pazaak, courses de swoop, la tourelle) n'ont pas encore d'assignations manette et gardent leurs touches clavier.

Les noms de boutons ci-dessous suivent la disposition Xbox : A, B, X, Y, les boutons de tranche gauche et droit (LB / RB), les gâchettes gauche et droite (LT / RT), et l'appui sur un stick (L3 / R3).

<h3>Déplacement et orientation</h3>

- Stick gauche — Se déplacer (les huit directions : avant, arrière et pas chassé)
- Stick droit — Tourner la caméra
- R3 (appuyer sur le stick droit) — Tourner la caméra vers le prochain point de passage de la balise, sinon vers le point cardinal suivant (le N du clavier)
- Gâchette droite seule — Annoncer l'orientation en degrés

<h3>Objets et actions</h3>

- Croix directionnelle gauche / droite — Objet découvert précédent / suivant (le cycle `,` / `.` du clavier)
- Croix haut / bas — Catégorie d'objets précédente / suivante
- A — Action par défaut sur la cible ciblée (attaquer, ouvrir, parler, ramasser)
- B — Fermer le menu d'actions s'il est ouvert ; sinon l'annulation normale du moteur
- Boutons de tranche gauche / droit — Changer de cible vers la gauche / la droite (le Q / E du clavier)
- Gâchette gauche seule — Ouvrir le menu d'actions unifié ; un nouvel appui le ferme. Tant qu'il est ouvert : croix gauche / droite changent de catégorie, haut / bas d'entrée, A déclenche l'action choisie
- Gâchette gauche + bouton de tranche gauche — Balise audio vers l'objet ciblé
- Gâchette droite + bouton de tranche droit — Marcher automatiquement jusqu'à l'objet ciblé

<h3>Groupe, état et jeu</h3>

- X — Changer de chef de groupe
- Gâchette gauche + X — Votre propre état (le H du clavier)
- Gâchette droite + X — La file d'actions (le Maj+H du clavier). À l'intérieur : croix haut / bas parcourent les entrées, A retire la dernière action mise en file, gâchette gauche + A vide toute la file, B ferme
- Y — Menu rapide, avec les entrées : les écrans de menu, chef de groupe, mode solo, discrétion, sauvegarde rapide et Aide, qui ouvre la liste des touches du mod. Le personnage s'arrête tant qu'il est ouvert
- L3 (appuyer sur le stick gauche) — Faire tournoyer l'arme
- Start — Pause
- Bouton Retour (Back) — Options
- Les deux gâchettes (LT + RT) — Lire les touches de l'écran actuel (le Ctrl+F1 du clavier)

<h3>Dans les menus</h3>

Dans les menus du jeu, la manette se comporte simplement comme le clavier :

- Croix directionnelle ou stick gauche — Déplacer le focus
- A — Valider, B — Retour
- Boutons de tranche gauche / droit — Parcourir les sous-écrans du menu du jeu (équipement, carte, quêtes, …) — c'est ainsi que la manette atteint la carte ; dans un conteneur ou une boutique, ils changent plutôt le mode (prendre / donner, acheter / vendre)
- Maintenir Y et appuyer sur la croix haut ou bas — Lire la description complète de l'entrée ciblée, bloc par bloc, sans se déplacer
- Sur l'écran de la carte, le stick gauche déplace le curseur de la carte et la croix parcourt les marqueurs de carte

<h2>Les systèmes de navigation en un coup d'œil</h2>

KOTOR est un jeu de rôle en 3D : vous passez donc l'essentiel de votre temps à vous déplacer dans des pièces et autour d'objets. Le mod superpose quelques systèmes pour que vous restiez orienté — chacun s'annonce lui-même à l'usage.

<h3>Changement de cible — Q / E</h3>

Votre principal moyen de trouver des choses et d'agir dessus. Q / E parcourent les créatures, les portes et les objets utilisables que la caméra peut voir ; ce qui est ciblé est ce sur quoi agissent Entrée et les touches d'action 1–7. Le mod annonce chaque nouvelle cible.

<h3>Cycle des objets découverts — `,` / `.`</h3>

Pour revenir à ce que vous avez déjà trouvé. `,` / `.` parcourent tous les objets que vous avez découverts dans la zone actuelle — portes, conteneurs, personnages, transitions, points de repère, vos propres marqueurs — regroupés par catégorie. Annoncez-en un, marchez-y automatiquement, ou armez une balise audio. (Paramètres du mod → « Sélection d'objets sur toute la carte » l'élargit à ce que vous n'avez pas encore trouvé.)

<h3>Menu d'actions unifié — Maj+Entrée</h3>

Un seul menu contenant toutes les actions pour la cible actuelle — attaquer, parler, pouvoirs de Force, objets, capacités spéciales. Les flèches y déplacent le focus, Entrée active. Il remplace les menus radial, de cible et personnel séparés du jeu.

<h3>Carte — M</h3>

La carte du jeu, rendue navigable. Déplacez le curseur avec les flèches pour lire le terrain et les marqueurs, ou parcourez les marqueurs de la carte avec `,` / `.` dans le même vocabulaire que dans le monde. Le brouillard de guerre est respecté, et Maj+N pose un marqueur personnel au curseur.

<h3>Repères de murs et descriptions de forme des pièces</h3>

Pendant que vous vous déplacez, une couche audio 3D continue joue de légers clics positionnels renvoyés par les murs les plus proches — les murs proches sonnent plus fort — pour que vous gardiez une sensation constante de l'espace autour de vous. Et entrer dans une pièce en annonce le nom, la forme (couloir, carrefour, cul-de-sac, espace ouvert) et les sorties visibles, le tout calculé en direct à partir du maillage de déplacement du jeu.

<h2>Paramètres du mod</h2>

Presque tous les systèmes ajoutés par le mod peuvent être ajustés, activés ou désactivés en cours de jeu. Ouvrez l'écran Options du jeu (O) et choisissez **Paramètres du mod** tout en bas de la liste. Haut / Bas parcourent les lignes, Entrée bascule un réglage ou ouvre un sous-menu, Gauche / Droite déplacent un curseur de réglage, et Échap revient en arrière.

- Sélection d'objets sur toute la carte — élargit le cycle `,` / `.` aux objets que vous n'avez pas encore découverts
- Descriptions de la forme des salles — l'annonce du nom, de la forme et des sorties quand vous entrez dans une pièce
- Sons de mur — la couche continue de repères sonores positionnels sur les murs
- Lire les sous-titres des locuteurs doublés — annoncer les sous-titres des répliques doublées
- Visée automatique — l'aide à la visée du mini-jeu de tourelle
- Ignorer les vidéos d'introduction — prend effet au prochain lancement
- Volume des indices sonores et Volume des annonces vocales — curseurs de volume pour les sons et la parole du mod
- Raccourcis clavier — réassigner les touches ajoutées par le mod
- Glossaire audio — joue chaque repère sonore du mod, un par un avec son nom, pour que vous appreniez ce que chaque son signifie

<h2>Dépannage</h2>

<h3>Aucune parole après le lancement du jeu</h3>

- Assurez-vous que votre lecteur d'écran est lancé avant de démarrer KOTOR.
- Le moteur vocal Prism est fourni avec le mod et placé automatiquement par l'installeur. En cas d'installation manuelle, vérifiez que ses fichiers sont présents dans le dossier du jeu.
- Consultez le journal de patch le plus récent dans `<installation>\logs\patch-*.log` (le bouton **Collecter les journaux** de l'installeur le rassemble pour vous).

<h3>Le jeu plante au démarrage, ou le mod ne se charge pas</h3>

- Lancez l'installeur en tant qu'administrateur — il déploie le proxy `dinput8.dll` qui charge automatiquement le mod au démarrage du jeu.
- Vérifiez que votre version du jeu est prise en charge (voir « Versions du jeu non prises en charge » ci-dessus). L'installeur vérifie l'empreinte de `swkotor.exe` et vous préviendra en cas de différence.
- Si le jeu a été mis à jour récemment, relancez l'installeur — une mise à jour peut écraser le chargeur.

<h3>Le mod fonctionnait mais s'est arrêté après une mise à jour du jeu</h3>

- Les mises à jour Steam et GoG peuvent écraser les fichiers du chargeur du mod. Relancez l'installeur pour redéployer le mod.

<h3>Les raccourcis clavier ne fonctionnent pas</h3>

- Vérifiez que la fenêtre du jeu a le focus (basculez-y avec Alt+Tab).
- Appuyez sur F1. Si vous entendez la liste des touches, le mod est actif.
- Certaines touches ne fonctionnent que dans un contexte précis (les touches Pazaak seulement sur le plateau de Pazaak, les touches de sous-menu seulement dans un sous-menu du mod, etc.).

<h3>La caméra ne tourne plus avec les touches</h3>

- Vous avez probablement activé le mode vue libre du jeu. La disposition par défaut du jeu indique Verr. Maj comme bascule de la vue libre, il est donc facile de l'activer par accident — réappuyez sur Verr. Maj. Sinon, vérifiez les réglages de souris et de caméra dans les Options.
- Vérifiez aussi que vous n'êtes pas dans le mode vue du mod (B), où A / D font pivoter la caméra au lieu de déplacer votre personnage. Appuyez de nouveau sur B pour en sortir.

<h3>Mauvaise langue</h3>

- Le mod choisit sa langue automatiquement d'après la langue de votre jeu (lue dans le `dialog.tlk` du jeu). Il n'y a pas encore de sélecteur de langue en jeu : pour changer la langue du mod, installez le jeu dans cette langue. Les cinq langues dans lesquelles KOTOR est publié — anglais, français, allemand, italien, espagnol — sont prises en charge.

<h3>Windows signale l'installeur ou la DLL comme dangereux</h3>

L'installeur et le mod ne sont pas signés numériquement. Les certificats de signature de code coûtent plusieurs centaines d'euros par an, ce qui n'est pas réaliste pour un projet d'accessibilité gratuit ; Windows SmartScreen vous avertira donc au premier lancement de l'installeur et pourra signaler les fichiers comme provenant d'un éditeur inconnu.

Pour vérifier que le fichier téléchargé correspond à celui publié sur GitHub, chaque version publiée indique une somme de contrôle SHA-256. Vous pouvez calculer l'empreinte de votre téléchargement et la comparer :

- PowerShell : `Get-FileHash <nom_du_fichier> -Algorithm SHA256`
- Invite de commandes : `certutil -hashfile <nom_du_fichier> SHA256`

Si l'empreinte correspond à celle des notes de version, le fichier est authentique. Pour passer l'avertissement SmartScreen, choisissez « Informations complémentaires » puis « Exécuter quand même ».

<h2>Signaler des bugs</h2>

L'écran d'après-installation de l'installeur comporte un bouton **Collecter les journaux** qui compresse le journal de patch le plus récent et tout rapport d'erreur Windows dans votre dossier Téléchargements. Joignez cette archive à un [ticket GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) et décrivez ce que vous étiez en train de faire. Si vous pouvez reproduire un plantage, précisez dans quelle zone vous étiez — l'annonce de pièce ou de zone l'aura nommée juste avant.

<h2>Problèmes connus</h2>

Pour l'état actuel des bugs, des fonctionnalités prévues et des aspérités, voir [docs/known-issues.md](known-issues.md).

<h2>Précisions</h2>

<h3>Autres accessibilités</h3>

Pour l'instant, il s'agit d'un mod d'accessibilité pour lecteurs d'écran. Je suis un développeur totalement aveugle, et l'accès par lecteur d'écran est le domaine que je connais. J'aimerais sincèrement couvrir d'autres handicaps — basse vision, troubles moteurs, etc. — mais des questions comme la couleur, le contraste et la police restent abstraites pour moi en tant que personne totalement aveugle. Si vous avez besoin de quelque chose dans cette direction et que vous pouvez décrire clairement vos besoins et aider à tester le résultat, contactez-moi. Je serais heureux que le mod soit à la hauteur de son nom pour davantage de personnes.

<h3>Usage de l'IA</h3>

Le code de ce mod est écrit avec l'aide substantielle de Claude d'Anthropic, avec les modèles Opus (le développement a couvert les générations Opus 4.5, 4.6 et 4.7). Je connais les débats autour du développement assisté par IA. Mais à une époque où l'industrie du jeu n'a jamais livré l'accessibilité dont nous avons besoin — ni en qualité ni en quantité — pour des titres comme KOTOR, ces outils sont ce qui rend un projet de cette taille faisable pour un développeur aveugle seul. Chaque changement est relu et testé en jeu, à l'oreille, avant d'être publié.

<h2>Comment contribuer</h2>

Les contributions sont bienvenues — en particulier les corrections pour les langues, les configurations système ou les lecteurs d'écran que je ne peux pas tester localement. Les demandes de fonctionnalités sont également les bienvenues. Avant de commencer, parcourez le fichier des problèmes connus ci-dessus pour voir si votre idée figure déjà au backlog.

- Guide de contribution : [CONTRIBUTING.md](../CONTRIBUTING.md)
- Vue d'ensemble de l'architecture : [ARCHITECTURE.md](../ARCHITECTURE.md)
- Guide du modding d'accessibilité (savoir-faire général + spécificités des binaires natifs) : [ACCESSIBILITY_MODDING_GUIDE.md](../ACCESSIBILITY_MODDING_GUIDE.md)
- Traduire les annonces vocales du mod : [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md)

<h2>Remerciements</h2>

Et maintenant, je veux remercier tout un tas de gens. D'abord, ce mod repose massivement sur le travail de la communauté, et beaucoup de personnes ont fait les choses vraiment difficiles, si bien que je n'ai eu qu'à reprendre leurs outils et à créer ma propre chose avec. Ensuite, heureusement, ce n'était pas seulement moi et l'IA dans une boîte noire, mais tout un réseau autour de moi, qui a aidé, encouragé, et qui a simplement été social et gentil.

Écrivez-moi si je vous ai oublié, ou si vous souhaitez être mentionné sous un autre nom ou pas du tout.

La rétro-ingénierie et le framework de patch sur lesquels repose ce mod viennent de **Lane Dibello**, dont le [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) et le travail sous Ghidra ont rendu possible le fait même de hooker le jeu. Merci aussi à la communauté de modding KOTOR autour de **DeadlyStream**, qui a compris les outils et les formats que je n'ai eu qu'à reprendre et utiliser.

Beaucoup de personnes m'ont aidé à tester le jeu, ont trouvé tous les bugs que je ne pouvais pas trouver et m'ont donné des retours pour cerner les faiblesses et les désagréments du projet. Sans elles, cela ne serait jamais devenu un projet. Je veux donc remercier :

- Kenny
- Berenion
- unexplained entity
- Grinvold
- dansc93
- Mojsior
- stirlock
- Druidah
- SightlessKombat
- Destranis
- Ozuaw
- mkdbzfan
- Kamilana
- ABlindFellow
- zargontheevilgod
- zersiax

**Fondations et dépendances :**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), la base Ghidra rétro-ingéniérée et le framework de patch
- **Prism** (Ethin P.) — pont vocal multiplateforme couvrant tous les grands lecteurs d'écran, avec repli SAPI
- Équipe du **K1 Community Patch** (KOTORCommunityPatches) — couche de corrections de bugs intégrée
- **xoreos / xoreos-tools** — réimplémentation open source du moteur ; référence croisée pour les formats de fichiers
- Communauté **DeadlyStream** — base de connaissances du modding

<h3>Outils utilisés</h3>

- Claude (Anthropic) — partenaire de programmation en binôme sur les générations Opus 4.5, 4.6 et 4.7
- Kotor-Patch-Manager — framework de patch par injection de DLL à l'exécution
- Prism — pont vocal pour lecteurs d'écran
- Tolk — bibliothèque pour lecteurs d'écran (chemin de repli)
- Ghidra — rétro-ingénierie
- xoreos-tools — extraction sans interface des formats de fichiers du jeu
- K1 Community Patch — couche communautaire de corrections de bugs intégrée

<h2>Soutenir votre moddeur</h2>

Créer ce mod a été très amusant et une vraie source d'autonomisation, mais cela a aussi coûté beaucoup de temps et de l'argent réel en abonnements Claude. J'ai l'intention de les maintenir pour entretenir le projet et l'améliorer dans les années à venir. Si vous en avez la possibilité et l'envie, un don ponctuel ou récurrent serait profondément apprécié — il reconnaît le travail et me donne une base stable pour continuer d'améliorer Voice of the Old Republic et, je l'espère, d'autres grands projets d'accessibilité.

[Ko-fi : ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>Licence</h2>

Le code source du mod est sous licence GNU General Public License v3 (voir [LICENSE](../LICENSE)). Les dépendances intégrées sous `third_party/` conservent leurs propres licences (Prism est en MPL-2.0 ; Tolk en LGPL ; Kotor-Patch-Manager est intégré selon les termes de son projet d'origine ; dsoal et OpenAL Soft, quand le chemin audio spatial optionnel est activé, sont en LGPL-2.1). Le jeu lui-même et les fichiers de données de BioWare ne sont pas redistribués par ce projet.

<h2>Liens</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Signaler un problème](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (soutenir le projet)](https://ko-fi.com/jeanstiletto)

<h2>Autres langues</h2>

- [English](/voice-of-the-old-republic/)
- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)
- [Polski](/voice-of-the-old-republic/docs/README.pl.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Les traductions se trouvent dans `docs/README.{de,fr,it,es,pl,ru}.md`. Pour améliorer ou ajouter une traduction, voir [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md).
