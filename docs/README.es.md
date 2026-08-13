---
layout: default
title: Voice of the Old Republic — Español
permalink: /docs/README.es.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <strong>Español</strong> · <a href="/voice-of-the-old-republic/docs/README.pl.html">Polski</a> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>Qué es este mod</h2>

**Voice of the Old Republic** es un proyecto para hacer accesibles los juegos Star Wars: Knights of the Old Republic a jugadores totalmente ciegos. Usa el puente de voz Prism para admitir cualquier lector de pantalla y añade ayudas de navegación, accesibilidad de los menús y señales sonoras específicas que hacen accesibles los minijuegos.

En el estado actual, Knights of the Old Republic I es totalmente jugable y completable, con todos los minijuegos, misiones y estilos de juego. Se puede jugar con el teclado o con un mando de estilo Xbox — el mod incluye su propio soporte de mandos (ver la sección del mando más abajo); solo los minijuegos siguen requiriendo el teclado.

Knights of the Old Republic II está portado: el instalador también configura el mod para él y el juego habla. Todavía no se ha completado, y habrá que eliminar algunos obstáculos antes de que sea posible — sus minijuegos y los mensajes del tutorial aún no están cubiertos.

El mod está traducido a todos los idiomas admitidos — inglés, alemán, francés, italiano, español — y además admite una traducción polaca y una rusa. Para Knights of the Old Republic I es compatible con la versión 1.03 de Steam y GoG y con la versión de 2004 que usan las traducciones polaca y rusa. Para Knights of the Old Republic II solo es compatible con la versión de Aspyr de 2015: la que distribuyen hoy tanto Steam como GoG.

<h2>Qué son los juegos Knights of the Old Republic</h2>

Knights of the Old Republic es una pareja de juegos de rol de Star Wars guiados por la historia, ambientados unos cuatro mil años antes de las películas. El primero lo hizo BioWare en 2003; el segundo — Knights of the Old Republic II: The Sith Lords — Obsidian en 2004, y transcurre unos años después de la parte I. Ambos funcionan sobre el mismo motor, y por eso el mod puede pasar de uno al otro.

En los dos juegos creas tu propio personaje, reúnes un grupo de compañeros, viajas entre planetas y das forma a la historia con tus decisiones de diálogo y según sigas el lado luminoso o el lado oscuro de la Fuerza.

El combate funciona igual en ambos: en tiempo real con pausa, construido sobre las reglas de mesa de Star Wars d20 — pones acciones en cola y los dados las resuelven. Cada asalto dura 6 segundos reales y puedes pausar en cualquier momento para examinar el campo de batalla y poner en cola acciones especiales mucho más fuertes que los ataques automáticos del personaje. Puedes encolar hasta 4 acciones por personaje antes de tener que dejar correr el combate, y puedes cancelar algunas de ellas si los acontecimientos de la pelea te obligan a cambiar de estrategia.

<h2>Requisitos</h2>

- Windows 10 o posterior
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam o GoG; para nuestros fines son idénticos byte a byte)
- Un lector de pantalla. La voz se encamina a través de Prism, que admite todos los lectores de pantalla en uso activo; si tu lector de pantalla funciona con cualquier otra cosa de tu sistema, funcionará con este mod
- Unos 200 MB de espacio libre en disco para el runtime del parcheador, el K1 Community Patch y el motor de voz incluido

<h3>Versiones del juego no admitidas en esta versión</h3>

- Ports Aspyr para móvil / macOS (binario distinto)
- Ejecutables ya parcheados (modificados por UniWS, por KOTOR High-Resolution Menus)
- Compilaciones cuyo SHA-256 de `swkotor.exe` no coincide con los hashes reconocidos de Steam o GoG 1.0.3

Si el instalador informa de una discrepancia de versión, abre una incidencia indicando el hash mostrado. La base de datos de direcciones cubre Steam y GoG de serie, y añadir un nuevo repack idéntico byte a byte suele ser un cambio de una línea en el manifiesto.

<h2>Instalación</h2>

1. Descarga `VoiceOfTheOldRepublicInstaller.exe` de la última versión publicada en GitHub
2. Cierra KOTOR si está en ejecución
3. Haz clic derecho en el instalador y elige **Ejecutar como administrador**. En el primer arranque, Windows SmartScreen avisará de un "editor desconocido" — pulsa **Más información → Ejecutar de todas formas**. El instalador aún no está firmado digitalmente, así que este aviso es normal (consulta la sección Solución de problemas para verificar la descarga)
4. (Recomendado) Haz una copia de tu carpeta de partidas guardadas en `%USERPROFILE%\Documents\Swkotor\saves\` antes de instalar si tienes una partida en curso
5. Recorre las pantallas del instalador. Detectará tu instalación de KOTOR, instalará el framework de parches, desplegará el mod y, de forma predeterminada, incluirá el K1 Community Patch junto con las correcciones de pantalla panorámica / menús de alta resolución
6. Inicia el juego desde la última pantalla del instalador o desde Steam

<h2>Desinstalación</h2>

Ejecuta de nuevo el instalador y elige la opción de desinstalar, o usa "Agregar o quitar programas". El desinstalador elimina solo los archivos de este mod — K1CP y cualquier otro mod opcional que eligieras al instalar se quedan donde están. Para volver a un KOTOR totalmente original, usa "Verificar integridad de los archivos del juego" de Steam o reinstala desde GoG después de desinstalar.

<h2>Primeros pasos</h2>

Cuando empiezas una partida nueva, KOTOR te lleva primero por la **creación de personaje**: eliges una clase (Soldado, Explorador o Ladrón), un retrato, y ajustas tus atributos, habilidades y dotes. El mod lee cada panel a medida que lo recorres — tómate tu tiempo, nada va contrarreloj.

Después despiertas en la **Espiral Endar**, una nave de la República bajo ataque. Es la zona tutorial del juego. El mod sustituye las ventanas de tutorial en pantalla por indicaciones de teclado escritas para usuarios de lector de pantalla, así aprendes los controles sobre la marcha. Sigue a Trask, tu guía, hacia las cápsulas de escape.

Algunas costumbres que hacen el inicio mucho más fácil:

- **Encuentra cosas con Q / E** y vuelve a lo que ya has encontrado con el ciclo `,` / `.` (ver los atajos de teclado más abajo).
- **Pulsa H** en cualquier momento para oír tu salud y tu estado, y **F1** para la lista completa de teclas.
- **Escucha la sala.** Entrar en una sala anuncia su nombre, su forma y sus salidas, y una capa de sonido suave te mantiene al tanto de las paredes más cercanas mientras te mueves.
- **Mira los ajustes pronto.** Pulsa O para las Opciones del juego. En Jugabilidad están las opciones de pausa automática, que deciden cuándo se detiene el combate por sí solo — conviene configurarlas antes de tu primer combate de verdad — y la asignación de teclas.
- **Y al final de la lista de Opciones: Ajustes del mod.** Casi todo lo que añade este mod puede ajustarse o activarse y desactivarse ahí, las teclas del mod pueden reasignarse, y el glosario de audio reproduce cada señal sonora con su nombre para que aprendas qué significa cada una.

Después de la Espiral Endar llegas a **Taris**, el primer mundo grande, y a partir de ahí la historia se abre.

<h2>Atajos de teclado</h2>

El mod deja intacto el mapa de teclas predeterminado del juego, con un cambio ergonómico que el instalador aplica en una instalación nueva (ver la nota sobre desplazamiento lateral / rotación de cámara más abajo). Todo lo que no aparezca aquí se comporta como en el juego sin modificar. Las teclas propias del juego se pueden reasignar en Opciones → Jugabilidad → Asignación de teclas, o directamente en `swkotor.ini`. Las teclas que añade el mod se pueden reasignar en Ajustes del mod → Asignación de teclas.

<h3>Teclas del juego que más usarás</h3>

- W / S — Avanzar / retroceder
- A / D — Desplazamiento lateral a la izquierda / derecha
- Z / C — Girar la cámara a la izquierda / derecha (**Y / C** en un teclado alemán QWERTZ)
- Q / E — Cambiar de objetivo hacia la izquierda / derecha
- R — Acción predeterminada sobre el objetivo actual (atacar, hablar, abrir)
- 1 / 2 / 3 — Usar las tres acciones del menú de acciones del objetivo
- 4 / 5 / 6 / 7 — Usar las ranuras de poder de la Fuerza / medipac / objeto / mina del jugador
- Tab — Cambiar de líder del grupo
- F — Cancelar el combate, G — Sigilo, V — Modo en solitario, X — Florear el arma
- Barra espaciadora — Pausa
- Esc — Menú del juego
- F4 — Guardado rápido, F5 — Carga rápida (en el menú principal, F5 busca en su lugar una actualización del mod)
- I — Inventario del grupo, U — Equipo, P — Ficha de personaje, K — Habilidades / dotes / poderes
- M — Mapa, L — Misiones, J — Mensajes, O — Opciones
- Ratón 1 — Clic en el mundo 3D (rara vez necesario; ver el modo vista más abajo)

> **Desplazamiento lateral / rotación de cámara:** los valores predeterminados de KOTOR son Z / C para desplazarse de lado y A / D para girar la cámara. El instalador de accesibilidad los intercambia en una instalación completa nueva para que ambos formen un grupo cómodo en la fila inferior — **desplazamiento lateral en A / D, rotación de cámara en Z / C**. Esa es la asignación que aparece arriba. Si actualizaste desde una instalación existente, o si reasignas tú mismo las teclas en `swkotor.ini`, tus teclas pueden ser distintas.

<h3>Teclas del mod — interacción con el mundo</h3>

- Intro — Lanzar la acción predeterminada sobre el objetivo anunciado actualmente (igual que un clic de Ratón 1 en el mundo)
- Mayús+Intro — Abrir el menú de acciones unificado del objetivo actual (todas las acciones — atacar, hablar, poderes de la Fuerza, objetos, habilidades especiales — en un solo menú)
- Mayús+1 … Mayús+7 — Abrir una categoría de acciones para elegir dentro de ella (1–3 son las acciones del objetivo, 4–7 tus poderes de la Fuerza / objetos / minas)
- H — Anunciar tu salud, tus efectos activos y el arma equipada
- Mayús+H — Abrir la cola de acciones (revisar o vaciar las acciones en cola)
- Mayús+L — Abrir el panel de subida de nivel
- F1 — Abrir o cerrar la lista completa de teclas; Ctrl+F1 — leer las teclas de la pantalla actual
- Ctrl+R — Copiar al portapapeles el último texto leído (una línea de diálogo, una entrada del diario, el nombre de un PNJ: lo último que se haya leído)

<h3>Teclas del mod — ciclo de objetos descubiertos</h3>

Un segundo ciclo, además de Q / E, que recorre los objetos que ya has descubierto en la zona actual — puertas, contenedores, personajes, transiciones de zona, puntos de referencia y tus propias marcas de mapa — agrupados por categoría. (Activa "Selección de objetos en todo el mapa" en los Ajustes del mod para incluir también lo que aún no has encontrado.)

- `,` / `.` — Objeto anterior / siguiente en la categoría actual
- Mayús+`,` / Mayús+`.` — Categoría anterior / siguiente (criaturas, puertas, contenedores, transiciones, marcas de mapa, …)
- Ctrl+`,` / Ctrl+`.` — Saltar al objeto más cercano / más lejano de la categoría
- `/` (distribución de EE. UU.) o `-` (distribución alemana) — Anunciar el objeto enfocado actualmente
- Mayús+`/` (Mayús+`-`) — Caminar automáticamente hasta ese objeto
- Ctrl+`/` (Ctrl+`-`) — Activar una baliza sonora que marca el camino mientras te mueves

<h3>Teclas del mod — orientación y grupo</h3>

- AltGr (Alt derecho, solo) — Anunciar la orientación actual como punto cardinal
- N — Girar la cámara 90° en sentido horario hacia el siguiente punto cardinal; si hay una baliza activa, apuntar en su lugar al siguiente punto de ruta de la baliza
- Tab — Anuncia el nuevo líder del grupo después de que el motor cambie el control

<h3>Teclas del mod — modo vista</h3>

Pulsa B para entrar en el modo vista. Mientras el modo vista está activo:

- A / D — Girar la cámara sin mover al personaje
- Intro — Interactuar con aquello a lo que apunta la cámara, o caminar automáticamente hasta ese punto
- Mayús+Intro — Abrir el menú de acciones sobre el objetivo de la cámara
- B de nuevo — Salir del modo vista

<h3>Teclas del mod — pantalla del mapa</h3>

Con el mapa del juego abierto:

- Flechas / Arriba / Abajo — Recorrer las notas y los puntos de referencia del mapa
- `,` / `.` — Recorrer las marcas del mapa (mismo vocabulario que el ciclo de objetos descubiertos)
- Mayús+N — Colocar una marca personal en la posición actual del cursor en el mundo (nombrada automáticamente según la sala o el punto de referencia más cercano). La nueva marca entra en el ciclo de inmediato y Ctrl+`-` pone una baliza sobre ella

<h3>Teclas del mod — submenús</h3>

Cuando hay un submenú del mod abierto (el menú de acciones unificado, un menú de categoría, la cola de acciones):

- Arriba / Abajo — Mover el foco
- Izquierda / Derecha — Moverse entre columnas o variantes
- Intro — Activar la fila enfocada
- Mayús+Intro — (solo en la cola de acciones) Vaciar todas las acciones en cola
- Esc — Cerrar el submenú

<h3>Teclas del mod — según el contexto</h3>

- Q o E dentro de un panel de Contenedor — Coger todo / dar objetos
- Q o E dentro de un panel de Tienda — Cambiar entre Comprar y Vender

Dentro del campo de nombre de la creación de personaje (y otros cuadros de texto):

- Arriba / Abajo — Releer el texto actual desde el principio
- Intro — Confirmar
- Esc — Cancelar

<h3>Teclas del mod — minijuego Pazaak</h3>

Con el tablero de Pazaak abierto:

- Arriba / Abajo — Moverse entre zonas: tu mano, tu mesa, la mesa del rival, las acciones (Plantarse / Terminar turno)
- Izquierda / Derecha — Moverse dentro de la zona actual (las ranuras vacías de la mano se saltan)
- Intro — Jugar la carta enfocada de la mano, o activar la acción enfocada
- S — Plantarse
- E — Terminar turno
- C — Leer tu mano
- T — Leer ambas mesas con sus totales
- Mayús+C — Cuántas cartas le quedan en la mano al rival
- Carta más/menos — Intro abre un selector de signo; Izquierda / Derecha eligen más o menos, Intro juega con ese signo, Esc cancela

En la pantalla de apuesta previa a la partida, la primera entrada lee tu apuesta actual, el máximo de la mesa y tus créditos; ve a "Reducir apuesta" / "Aumentar apuesta" y pulsa Intro para cambiarla, y luego al botón de apuesta del juego para colocarla. El editor del mazo lateral lee cada carta y cada ranura del mazo.

<h2>Mando</h2>

El mod incluye su propio soporte de mandos — KOTOR 1 no tiene ningún código de gamepad propio, así que es el mod el que controla el mando directamente. Funciona cualquier mando XInput con la disposición de botones de Xbox; conéctalo antes de iniciar el juego. El teclado y el mando permanecen activos a la vez, y cada acción del juego que dispara el mando pasa por tus propias asignaciones de teclas, así que las reasignaciones se respetan. Con un mando conectado, la lista de teclas (F1) gana una sección Mando; sin mando, esa sección desaparece. Los minijuegos (Pazaak, carreras de motos deslizadoras, la torreta) aún no tienen asignaciones de mando y conservan sus teclas de teclado.

Los nombres de botones de abajo siguen la disposición de Xbox: A, B, X, Y, los botones superiores izquierdo y derecho (LB / RB), los gatillos izquierdo y derecho (LT / RT), y la pulsación de una palanca (L3 / R3).

<h3>Movimiento y orientación</h3>

- Palanca izquierda — Moverse (las ocho direcciones: adelante, atrás y desplazamiento lateral)
- Palanca derecha — Girar la cámara
- R3 (pulsar la palanca derecha) — Girar la cámara hacia el siguiente punto de ruta de la baliza, o si no hacia el siguiente punto cardinal (la N del teclado)
- Gatillo derecho solo — Anunciar la orientación en grados

<h3>Objetos y acciones</h3>

- Cruceta izquierda / derecha — Objeto descubierto anterior / siguiente (el ciclo `,` / `.` del teclado)
- Cruceta arriba / abajo — Categoría de objetos anterior / siguiente
- A — Acción predeterminada sobre el objetivo enfocado (atacar, abrir, hablar, recoger)
- B — Cerrar el menú de acciones si está abierto; si no, la cancelación normal del motor
- Botón superior izquierdo / derecho — Cambiar de objetivo hacia la izquierda / derecha (la Q / E del teclado)
- Gatillo izquierdo solo — Abrir el menú de acciones unificado; al pulsarlo de nuevo se cierra. Mientras está abierto: cruceta izquierda / derecha cambian de categoría, arriba / abajo de entrada, A ejecuta la acción elegida
- Gatillo izquierdo + botón superior izquierdo — Baliza sonora hacia el objeto enfocado
- Gatillo derecho + botón superior derecho — Caminar automáticamente hasta el objeto enfocado

<h3>Grupo, estado y juego</h3>

- X — Cambiar de líder del grupo
- Gatillo izquierdo + X — Tu propio estado (la H del teclado)
- Gatillo derecho + X — La cola de acciones (el Mayús+H del teclado). Dentro de ella: cruceta arriba / abajo recorren las entradas, A quita la última acción en cola, gatillo izquierdo + A vacía toda la cola, B cierra
- Y — Menú rápido, con las entradas: las pantallas de menú, líder del grupo, modo en solitario, sigilo, guardado rápido y Ayuda, que abre la lista de teclas del mod. El personaje se detiene mientras está abierto
- L3 (pulsar la palanca izquierda) — Florear el arma
- Start — Pausa
- Botón Atrás (Back) — Opciones
- Ambos gatillos (LT + RT) — Leer las teclas de la pantalla actual (el Ctrl+F1 del teclado)

<h3>En los menús</h3>

En los menús del juego, el mando se comporta sencillamente como el teclado:

- Cruceta o palanca izquierda — Mover el foco
- A — Confirmar, B — Atrás
- Botón superior izquierdo / derecho — Recorrer las subpantallas del menú del juego (equipo, mapa, misiones, …) — así es como el mando llega al mapa; en un contenedor o una tienda cambian en su lugar el modo (coger / dar, comprar / vender)
- Mantener Y y pulsar la cruceta arriba o abajo — Leer la descripción completa de la entrada enfocada, bloque a bloque, sin moverse
- En la pantalla del mapa, la palanca izquierda desplaza el cursor del mapa y la cruceta recorre las marcas del mapa

<h2>Los sistemas de navegación de un vistazo</h2>

KOTOR es un juego de rol en 3D, así que pasarás la mayor parte del tiempo moviéndote por salas y alrededor de objetos. El mod superpone varios sistemas para que sigas orientado — cada uno se anuncia solo a medida que lo usas.

<h3>Cambio de objetivo — Q / E</h3>

Tu forma principal de encontrar cosas y actuar sobre ellas. Q / E recorren las criaturas, puertas y objetos utilizables que la cámara puede ver; lo que esté seleccionado es sobre lo que actúan Intro y las teclas de acción 1–7. El mod anuncia cada objetivo nuevo.

<h3>Ciclo de objetos descubiertos — `,` / `.`</h3>

Para volver a lo que ya has encontrado. `,` / `.` recorren todos los objetos que has descubierto en la zona actual — puertas, contenedores, personajes, transiciones, puntos de referencia, tus propias marcas — agrupados por categoría. Anuncia uno, camina automáticamente hasta él o activa una baliza sonora. (Ajustes del mod → "Selección de objetos en todo el mapa" lo amplía también a lo que aún no has encontrado.)

<h3>Menú de acciones unificado — Mayús+Intro</h3>

Un único menú con todas las acciones para el objetivo actual — atacar, hablar, poderes de la Fuerza, objetos, habilidades especiales. Las flechas mueven el foco, Intro activa. Sustituye a los menús radial, de objetivo y personal separados del juego.

<h3>Mapa — M</h3>

El mapa del juego de KOTOR, hecho navegable. Mueve el cursor con las flechas para leer el terreno y las marcas, o recorre las marcas del mapa con `,` / `.` con el mismo vocabulario que se usa en el mundo. Se respeta la niebla de guerra, y Mayús+N coloca una marca personal en el cursor.

<h3>Señales de paredes y descripciones de la forma de las salas</h3>

Mientras te mueves, una capa de audio 3D continua reproduce suaves clics posicionales rebotados en las paredes más cercanas — las paredes cercanas suenan más fuerte — para que mantengas una sensación constante del espacio que te rodea. Y al entrar en una sala se anuncian su nombre, su forma (pasillo, cruce, callejón sin salida, espacio abierto) y las salidas visibles, todo calculado en vivo a partir de la malla de desplazamiento del juego.

<h2>Ajustes del mod</h2>

Casi todos los sistemas que añade el mod se pueden ajustar o activar y desactivar mientras juegas. Abre la pantalla de Opciones del juego (O) y elige **Ajustes del mod** al final de la lista. Arriba / Abajo recorren las filas, Intro conmuta un ajuste o abre un submenú, Izquierda / Derecha mueven un deslizador, y Esc vuelve atrás.

- Selección de objetos en todo el mapa — amplía el ciclo `,` / `.` a los objetos que aún no has descubierto
- Descripciones de la forma de las habitaciones — el anuncio del nombre, la forma y las salidas al entrar en una sala
- Sonidos de pared — la capa continua de señales sonoras posicionales de las paredes
- Leer subtítulos de hablantes con voz — anunciar los subtítulos de las líneas dobladas
- Apuntado automático — la ayuda de puntería del minijuego de la torreta
- Omitir vídeos de introducción — surte efecto en el siguiente arranque
- Volumen de los sonidos de aviso y Volumen de los anuncios hablados — deslizadores de volumen para los sonidos y la voz del mod
- Asignación de teclas — reasignar las teclas que añade el mod
- Glosario de audio — reproduce cada señal sonora del mod, una a una y con su nombre, para que aprendas qué significa cada sonido

<h2>Solución de problemas</h2>

<h3>No hay voz después de iniciar el juego</h3>

- Asegúrate de que tu lector de pantalla esté en marcha antes de iniciar KOTOR.
- El motor de voz Prism se incluye con el mod y el instalador lo coloca automáticamente. Si instalaste manualmente, comprueba que sus archivos estén en la carpeta del juego.
- Revisa el registro de parche más reciente en `<instalación>\logs\patch-*.log` (el botón **Recopilar registros** del instalador lo reúne por ti).

<h3>El juego se cierra al arrancar, o el mod no carga</h3>

- Ejecuta el instalador como administrador — despliega el proxy `dinput8.dll` que carga el mod automáticamente al iniciar el juego.
- Confirma que tu versión del juego está admitida (ver "Versiones del juego no admitidas" más arriba). El instalador comprueba el hash de `swkotor.exe` y te avisará si no coincide.
- Si el juego se actualizó hace poco, vuelve a ejecutar el instalador — una actualización puede sobrescribir el cargador.

<h3>El mod funcionaba pero dejó de hacerlo tras una actualización del juego</h3>

- Las actualizaciones de Steam y GoG pueden sobrescribir los archivos del cargador del mod. Ejecuta de nuevo el instalador para volver a desplegar el mod.

<h3>Los atajos de teclado no funcionan</h3>

- Asegúrate de que la ventana del juego tenga el foco (cambia a ella con Alt+Tab).
- Pulsa F1. Si oyes la lista de teclas, el mod está activo.
- Algunas teclas solo funcionan en un contexto concreto (las teclas de Pazaak solo en el tablero de Pazaak, las de submenú solo dentro de un submenú del mod, etc.).

<h3>La cámara ya no gira con las teclas</h3>

- Probablemente has activado el modo de vista libre del juego. El mapa de teclas predeterminado del juego indica Bloq Mayús como interruptor de la vista libre, así que es fácil pulsarlo sin querer — prueba a pulsar Bloq Mayús otra vez. Si no, revisa los ajustes de ratón y cámara en las Opciones.
- Comprueba también que no estés en el modo vista del mod (B), donde A / D giran la cámara en lugar de mover al personaje. Pulsa B de nuevo para salir.

<h3>Idioma incorrecto</h3>

- El mod elige su idioma automáticamente a partir del idioma de tu juego (leído del `dialog.tlk` del juego). Todavía no hay selector de idioma en el juego, así que para cambiar el idioma del mod instala el juego en ese idioma. Los cinco idiomas en los que se publica KOTOR — inglés, francés, alemán, italiano, español — están admitidos.

<h3>Windows avisa de que el instalador o la DLL no son seguros</h3>

El instalador y el mod no están firmados digitalmente. Los certificados de firma de código cuestan cientos de euros al año, algo poco realista para un proyecto de accesibilidad gratuito, así que Windows SmartScreen te avisará la primera vez que ejecutes el instalador y puede marcar los archivos como procedentes de un editor desconocido.

Para verificar que el archivo que descargaste coincide con el publicado en GitHub, cada versión indica una suma de comprobación SHA-256. Puedes calcular el hash de tu descarga y compararlo:

- PowerShell: `Get-FileHash <archivo> -Algorithm SHA256`
- Símbolo del sistema: `certutil -hashfile <archivo> SHA256`

Si el hash coincide con el de las notas de la versión, el archivo es auténtico. Para saltar el aviso de SmartScreen, elige "Más información" y luego "Ejecutar de todas formas".

<h2>Informar de errores</h2>

La pantalla posterior a la instalación tiene un botón **Recopilar registros** que comprime el registro de parche más reciente y cualquier volcado de Windows Error Reporting en tu carpeta de Descargas. Adjunta ese zip a una [incidencia de GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) y describe qué estabas haciendo. Si puedes reproducir un cierre inesperado, menciona en qué zona estabas — el anuncio de sala o de zona la habrá nombrado justo antes.

<h2>Problemas conocidos</h2>

Para el estado actual de errores, funciones previstas y asperezas, consulta [docs/known-issues.md](known-issues.md).

<h2>Aclaraciones</h2>

<h3>Otras accesibilidades</h3>

Por ahora, este es un mod de accesibilidad para lectores de pantalla. Soy un desarrollador totalmente ciego, y el acceso mediante lector de pantalla es el terreno que conozco. Me gustaría de verdad cubrir más discapacidades — baja visión, discapacidad motora, etcétera — pero cuestiones como el color, el contraste y la tipografía son abstractas para mí como persona totalmente ciega. Si necesitas algo en esa dirección y puedes describir tus necesidades con claridad y ayudar a probar el resultado, ponte en contacto. Me encantaría que el mod hiciera honor a su nombre para más gente.

<h3>Uso de IA</h3>

El código de este mod está escrito con una asistencia importante de Claude, de Anthropic, con los modelos Opus (el desarrollo abarcó las generaciones Opus 4.5, 4.6 y 4.7). Conozco los debates en torno al desarrollo asistido por IA. Pero en un momento en el que la industria del videojuego nunca ha entregado la accesibilidad que necesitamos — ni en calidad ni en cantidad — para títulos como KOTOR, estas herramientas son lo que hace viable un proyecto de este tamaño para un único desarrollador ciego. Cada cambio se revisa y se prueba en el juego, de oído, antes de publicarse.

<h2>Cómo contribuir</h2>

Las contribuciones son bienvenidas — sobre todo correcciones para idiomas, configuraciones de sistema o lectores de pantalla que no puedo probar en local. También acepto peticiones de funciones. Antes de ponerte a trabajar, echa un vistazo al archivo de problemas conocidos de arriba para ver si tu idea ya está en la lista.

- Guía de contribución: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Visión general de la arquitectura: [ARCHITECTURE.md](../ARCHITECTURE.md)
- Guía de modding de accesibilidad (oficio general + particularidades de los binarios nativos): [ACCESSIBILITY_MODDING_GUIDE.md](../ACCESSIBILITY_MODDING_GUIDE.md)
- Traducir los avisos hablados del mod: [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md)

<h2>Agradecimientos</h2>

Y ahora quiero dar las gracias a un montón de gente. Lo primero: este mod se apoya enormemente en el trabajo de la comunidad, y mucha gente hizo las cosas realmente difíciles, así que yo solo tuve que recoger sus herramientas y crear lo mío con ellas. Además, por suerte, no fuimos solo la IA y yo en una caja negra, sino toda una red a mi alrededor que echó una mano, dio ánimos y simplemente fue social y amable.

Escríbeme por privado si me he olvidado de ti, o si quieres aparecer con otro nombre o no aparecer en absoluto.

La ingeniería inversa y el framework de parches sobre los que funciona este mod vienen de **Lane Dibello**, cuyo [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) y cuyo trabajo con Ghidra hicieron posible siquiera enganchar el juego. Gracias también a la comunidad de modding de KOTOR en torno a **DeadlyStream**, que descifró las herramientas y los formatos que yo solo tuve que recoger y usar.

Mucha gente me ayudó a probar el juego, encontró todos los errores que yo no podía encontrar y me dio comentarios para señalar las debilidades y las incomodidades del proyecto. Sin ellos no habría llegado a ser un proyecto siquiera. Así que quiero dar las gracias a:

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

**Cimientos y dependencias:**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), la base de datos de Ghidra obtenida por ingeniería inversa y el framework de parches
- **Prism** (Ethin P.) — puente de voz multiplataforma que cubre todos los lectores de pantalla principales, con respaldo SAPI
- Equipo del **K1 Community Patch** (KOTORCommunityPatches) — capa de corrección de errores incluida
- **xoreos / xoreos-tools** — reimplementación de código abierto del motor; referencia cruzada para los formatos de archivo
- Comunidad de **DeadlyStream** — base de conocimiento del modding

<h3>Herramientas usadas</h3>

- Claude (Anthropic) — compañero de programación en pareja a lo largo de las generaciones Opus 4.5, 4.6 y 4.7
- Kotor-Patch-Manager — framework de parcheo por inyección de DLL en tiempo de ejecución
- Prism — puente de voz para lectores de pantalla
- Tolk — biblioteca para lectores de pantalla (vía de respaldo)
- Ghidra — ingeniería inversa
- xoreos-tools — extracción sin interfaz de los formatos de archivo del juego
- K1 Community Patch — capa comunitaria de corrección de errores incluida

<h2>Apoya a tu modder</h2>

Construir este mod ha sido muy divertido y una fuente real de empoderamiento, pero también ha costado mucho tiempo y dinero real en suscripciones a Claude. Tengo la intención de mantenerlas para cuidar el proyecto y mejorarlo en los próximos años. Si puedes y quieres hacer una donación puntual o periódica, lo agradecería muchísimo — reconoce el trabajo y me da una base estable para seguir mejorando Voice of the Old Republic y, con suerte, otros grandes proyectos de accesibilidad.

[Ko-fi: ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>Licencia</h2>

El código fuente del mod se publica bajo la GNU General Public License v3 (ver [LICENSE](../LICENSE)). Las dependencias incluidas en `third_party/` conservan sus propias licencias (Prism es MPL-2.0; Tolk es LGPL; Kotor-Patch-Manager se incluye según los términos de su proyecto original; dsoal y OpenAL Soft, cuando se activa la vía opcional de audio espacial, son LGPL-2.1). El juego en sí y los archivos de datos de BioWare no son redistribuidos por este proyecto.

<h2>Enlaces</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Informar de un problema](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (apoyar el proyecto)](https://ko-fi.com/jeanstiletto)

<h2>Otros idiomas</h2>

- [English](/voice-of-the-old-republic/)
- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Polski](/voice-of-the-old-republic/docs/README.pl.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Las traducciones están en `docs/README.{de,fr,it,es,pl,ru}.md`. Para mejorar o añadir una traducción, consulta [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md).
