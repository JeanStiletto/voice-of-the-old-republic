---
layout: default
title: Voice of the Old Republic — Español
permalink: /docs/README.es.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <strong>Español</strong></p>

<h2>Qué es este mod</h2>

**Voice of the Old Republic** es un mod de lector de pantalla y navegación por teclado para **Star Wars: Knights of the Old Republic 1** (BioWare, 2003, versión de Steam) que permite a jugadores totalmente ciegos jugar a KOTOR 1 con cualquier lector de pantalla moderno. La síntesis de voz pasa por el puente de voz Prism, que admite todos los principales lectores de pantalla en todas las principales plataformas.

El mod está escrito por un desarrollador ciego. Cada flujo de trabajo — instalación, juego, contribución — está diseñado para poder hacerse solo con un lector de pantalla y un teclado.

<h2>Requisitos</h2>

- Windows 10 o posterior
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam o GoG; ambos son idénticos a nivel de bytes para nuestros propósitos)
- Un lector de pantalla. La síntesis de voz pasa por Prism, que admite todo el conjunto de lectores de pantalla en uso activo; si tu lector de pantalla funciona con cualquier otra cosa en tu sistema, funcionará con este mod
- Unos 200 MB de espacio libre en disco para el runtime del parcheador, el K1 Community Patch y el runtime de voz incluido

<h3>Versiones del juego no compatibles en esta release</h3>

- Ports Aspyr móvil / macOS (binario diferente)
- Ejecutables ya parcheados (modificados por UniWS, modificados por KOTOR High-Resolution Menus)
- Builds cuyo SHA-256 de `swkotor.exe` no coincide con los hashes Steam o GoG 1.0.3 reconocidos

Si el instalador informa de una incompatibilidad de versión, abre una incidencia con el hash mostrado. La base de datos de direcciones cubre Steam y GoG de serie, y añadir un nuevo repack idéntico a nivel de bytes suele ser un cambio de una línea en el manifiesto.

<h2>Instalación</h2>

1. Descarga `VoiceOfTheOldRepublicInstaller.exe` desde la última release en GitHub
2. Cierra KOTOR si está en ejecución
3. Haz clic derecho en el instalador y elige **Ejecutar como administrador**. En el primer arranque Windows SmartScreen advertirá de un «Editor desconocido» — haz clic en **Más información → Ejecutar de todas formas**. El instalador aún no está firmado digitalmente, por lo que este aviso es esperado
4. (Recomendado) Haz una copia de seguridad de tu carpeta de partidas guardadas en `%USERPROFILE%\Documents\Swkotor\saves\` antes de instalar si tienes una partida en curso
5. Recorre las pantallas del instalador. Detectará tu instalación de KOTOR, instalará el framework de parches, desplegará el mod y (por defecto) incluirá el K1 Community Patch más las correcciones de widescreen / menús de alta resolución
6. Inicia el juego desde la pantalla final del instalador o desde Steam

Para desinstalar, ejecuta el instalador de nuevo y elige la opción de desinstalación, o usa «Programas y características». El desinstalador elimina solo los archivos de este mod — K1CP y cualquier otro mod opcional que hayas elegido en la instalación se quedan en su sitio. Para volver a un KOTOR completamente vanilla, usa «Verificar la integridad de los archivos del juego» de Steam o reinstala desde GoG después de desinstalar.

<h2>Atajos de teclado</h2>

El mod mantiene intacto el mapa de teclas predeterminado del juego. Cualquier cosa no listada a continuación se comporta como en el juego sin modificar. Cada acción a continuación puede reasignarse en `swkotor.ini` (teclas del juego) o, para las teclas añadidas por el mod, podrá reasignarse desde una pantalla de configuración dentro del juego en una versión posterior.

<h3>Teclas del juego que más usarás</h3>

- W / S — Moverse adelante / atrás
- A / D — Rotar cámara izquierda / derecha
- Z / C — Desplazarse lateralmente izquierda / derecha
- Q / E — Ciclar objetivo izquierda / derecha
- R — Acción predeterminada sobre el objetivo actual (atacar, hablar, abrir)
- 1 / 2 / 3 — Usar las tres acciones del menú de acciones del objetivo actual
- 4 / 5 / 6 / 7 — Usar las ranuras de poder de la Fuerza / medpac / objeto / mina del jugador
- Tab — Cambiar líder del grupo
- F — Cancelar combate, G — Sigilo, V — Modo solo, X — Floreo de arma
- Barra espaciadora — Pausa
- Esc — Menú del juego
- F4 — Guardado rápido, F5 — Carga rápida
- I — Inventario del grupo, U — Equipo, P — Ficha del personaje, K — Habilidades / dotes / poderes
- M — Mapa, L — Misiones, J — Mensajes, O — Opciones
- Ratón 1 — Clic en el mundo 3D (raramente necesario; ver modo vista más abajo)

<h3>Teclas del mod — interacción con el mundo</h3>

- Intro — Activa la acción predeterminada sobre el objetivo actualmente anunciado (equivalente a un clic de Ratón 1 en el mundo)
- Shift+Intro — Abre el menú de acciones unificado para el objetivo actual (todas las acciones — atacar, hablar, poderes de la Fuerza, objetos, habilidades especiales — en un solo menú)
- Shift+1 … Shift+7 — Abre una categoría de acciones para elegir dentro de ella (1–3 son las acciones del objetivo, 4–7 tus poderes de la Fuerza / objetos / minas)
- H — Anuncia tu propia salud, efectos activos y arma equipada
- Ö (distribución alemana) / Acento grave (distribución US) — Lee el panel Examinar para el objetivo actual
- Shift+H — Abre la cola de acciones (revisar o vaciar las acciones en cola)
- Shift+L — Abre el panel de subida de nivel
- F1 — Abre o cierra la lista completa de teclas; Ctrl+F1 — lee las teclas de la pantalla actual

<h3>Teclas del mod — ciclo de objetos descubiertos</h3>

Un segundo ciclo, sobre Q / E, que recorre los objetos que ya has descubierto en la zona actual — puertas, contenedores, personajes, transiciones de zona, puntos de referencia y tus propios marcadores de mapa — agrupados por categoría. (Activa «Ciclo extendido» en los Ajustes del mod para incluir también lo que aún no has encontrado.)

- `,` / `.` — Objeto anterior / siguiente en la categoría actual
- Shift+`,` / Shift+`.` — Categoría anterior / siguiente (criaturas, puertas, contenedores, transiciones, marcadores de mapa, …)
- Ctrl+`,` / Ctrl+`.` — Salta al objeto más cercano / más lejano de la categoría
- `/` (distribución US) o `-` (distribución alemana) — Anuncia el objeto actualmente enfocado
- Shift+`/` (Shift+`-`) — Caminar automáticamente hacia ese objeto
- Ctrl+`/` (Ctrl+`-`) — Arma una baliza de audio que va indicando el camino mientras te mueves

<h3>Teclas del mod — orientación y grupo</h3>

- AltGr (Alt derecho, solo) — Anuncia la orientación actual como dirección cardinal
- N — Gira la cámara 90° en sentido horario hacia la siguiente dirección cardinal; si una baliza está armada, apunta en su lugar al próximo waypoint de la baliza
- Tab — Anuncia el nuevo líder del grupo después de que el motor cambia el control

<h3>Teclas del mod — modo vista</h3>

Pulsa B para entrar en modo vista. Mientras el modo vista está activo:

- A / D — Mueve la cámara sin mover al personaje
- Intro — Interactúa con aquello a lo que apunta la cámara, o caminar automáticamente hasta ese punto
- Shift+Intro — Abre el menú de acciones sobre el objetivo de la cámara
- B de nuevo — Salir del modo vista

<h3>Teclas del mod — pantalla del mapa</h3>

Mientras el mapa del juego está abierto:

- Flechas / Arriba / Abajo — Cicla a través de las notas y puntos de referencia del mapa
- `,` / `.` — Cicla los marcadores de mapa (mismo vocabulario que el ciclo de objetos descubiertos)
- Shift+N — Coloca un marcador de mapa personal en la posición mundo actual del cursor (nombrado automáticamente según la sala o el punto de referencia más cercano). El nuevo marcador entra en el ciclo inmediatamente y Ctrl+`-` colocará una baliza sobre él

<h3>Teclas del mod — submenús</h3>

Cuando un submenú del mod está abierto (el menú de acciones unificado, una categoría de acciones, la cola de acciones):

- Arriba / Abajo — Mueve el foco
- Izquierda / Derecha — Cambia entre columnas o variantes
- Intro — Activa la fila enfocada
- Shift+Intro — (solo cola de acciones) Vacía todas las acciones en cola
- Esc — Cierra el submenú

<h3>Teclas del mod — específicas del contexto</h3>

- Q o E dentro de un panel Contenedor — Tomar todo / dar objetos
- Q o E dentro de un panel Tienda — Cambiar entre Comprar y Vender

Dentro del campo de nombre de creación de personaje (y otras cajas de entrada de texto):

- Arriba / Abajo — Releer el texto actual desde el principio
- Intro — Enviar
- Esc — Cancelar

<h2>Sistemas de navegación de un vistazo</h2>

KOTOR es un RPG en 3D, así que pasas la mayor parte del tiempo moviéndote a través de salas y alrededor de objetos. El mod superpone varios sistemas para mantenerte orientado — cada uno se anuncia a sí mismo a medida que lo usas.

<h3>Ciclo de objetivos — Q / E</h3>

Tu forma principal de encontrar cosas y actuar sobre ellas. Q / E recorren las criaturas, puertas y objetos usables que la cámara puede ver; lo que está apuntado es sobre lo que actúan Intro y las teclas de acción 1–7. El mod anuncia cada nuevo objetivo.

<h3>Ciclo de objetos descubiertos — `,` / `.`</h3>

Para volver a las cosas que ya has descubierto. `,` / `.` recorren cada objeto que has descubierto en la zona actual — puertas, contenedores, personajes, transiciones, puntos de referencia, tus propios marcadores — agrupados por categoría. Anuncia uno, camina automáticamente hasta él, o arma una baliza de audio. (Ajustes del mod → «Ciclo extendido» lo amplía para incluir lo que aún no has encontrado.)

<h3>Menú de acciones unificado — Shift+Intro</h3>

Un solo menú con todas las acciones para el objetivo actual — atacar, hablar, poderes de la Fuerza, objetos, habilidades especiales. Las flechas mueven el foco, Intro activa. Reemplaza los menús radial, de objetivo y personal separados del juego.

<h3>Mapa — M</h3>

El mapa integrado en KOTOR, hecho navegable. Mueve el cursor con las flechas para leer el terreno y los marcadores, o cicla los marcadores del mapa con `,` / `.` con el mismo vocabulario usado en el mundo. La niebla de guerra se respeta, y Shift+N coloca un marcador personal en el cursor.

<h3>Pistas de paredes y descripciones de la forma de las salas</h3>

A medida que te mueves, una capa de audio 3D continua reproduce suaves clics posicionales desde las paredes más cercanas — las paredes más cercanas suenan más fuerte — para que mantengas una sensación constante del espacio a tu alrededor. Y entrar en una sala pronuncia su nombre, su forma (corredor, intersección, callejón sin salida, espacio abierto) y las salidas visibles, todo calculado en vivo a partir del walkmesh del juego.

<h2>Informar de errores</h2>

La pantalla posterior a la instalación del instalador tiene un botón **Recopilar registros** que comprime el registro de parche más reciente y cualquier volcado de Windows Error Reporting en tu carpeta de Descargas. Adjunta ese zip a una [incidencia en GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) y describe lo que estabas haciendo. Si puedes reproducir un fallo, menciona en qué zona estabas — el anuncio de sala o zona lo habrá dicho justo antes.

<h2>Problemas conocidos</h2>

Para el backlog actual de errores, características previstas y asperezas, consulta [docs/known-issues.md](known-issues.md).

<h2>Contribuir</h2>

Las contribuciones son bienvenidas — especialmente correcciones para idiomas, configuraciones de sistema o lectores de pantalla que el desarrollador no puede probar localmente. Antes de empezar el trabajo, hojea el archivo de problemas conocidos de arriba para ver si tu idea ya está en el backlog.

- Guía de contribución: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Visión general de la arquitectura: [ARCHITECTURE.md](../ARCHITECTURE.md)

<h2>Uso de IA</h2>

El código del mod se escribe con asistencia importante de Claude de Anthropic (serie Opus). En una industria de los videojuegos que históricamente se ha negado a entregar accesibilidad nativa para títulos como KOTOR, el modding asistido por IA es lo que hace que un proyecto de este tamaño sea factible para un único desarrollador ciego. Cada cambio es revisado y probado en el juego por el autor antes de ser entregado.

<h2>Licencia</h2>

El código fuente del mod está bajo la GNU General Public License v3 (consulta [LICENSE](../LICENSE)). Las dependencias incorporadas en `third_party/` mantienen sus propias licencias (Prism es MPL-2.0; Tolk es LGPL; Kotor-Patch-Manager se incorpora según los términos de su upstream; dsoal y OpenAL Soft, cuando la ruta opcional de spatial-audio está habilitada, son LGPL-2.1). El juego en sí y los archivos de datos de BioWare no son redistribuidos por este proyecto.

<h2>Créditos</h2>

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), la base de datos de Ghidra derivada de ingeniería inversa, y el framework de parches sobre el que funciona este mod
- **Prism** (Ethin P.) — puente de voz multiplataforma que cubre todos los principales lectores de pantalla, con respaldo SAPI
- El equipo del **K1 Community Patch** (KOTORCommunityPatches) — capa de corrección de errores incluida
- **xoreos / xoreos-tools** — reimplementación de código abierto del motor; referencia cruzada para formatos de archivo
- La comunidad **DeadlyStream** — base de conocimiento del modding
- **Claude (Anthropic)** — compañero de pair-programming a través de las generaciones Opus 4.5, 4.6 y 4.7
