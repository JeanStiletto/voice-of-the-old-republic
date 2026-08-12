---
layout: default
title: Voice of the Old Republic — Polski
permalink: /docs/README.pl.html
---
<h1>Voice of the Old Republic</h1>

<p><a href="/voice-of-the-old-republic/">English</a> · <a href="/voice-of-the-old-republic/docs/README.de.html">Deutsch</a> · <a href="/voice-of-the-old-republic/docs/README.fr.html">Français</a> · <a href="/voice-of-the-old-republic/docs/README.it.html">Italiano</a> · <a href="/voice-of-the-old-republic/docs/README.es.html">Español</a> · <strong>Polski</strong> · <a href="/voice-of-the-old-republic/docs/README.ru.html">Русский</a></p>

<h2>Czym jest ten mod</h2>

**Voice of the Old Republic** to projekt, który udostępnia gry Star Wars: Knights of the Old Republic osobom całkowicie niewidomym. Korzysta z mostka mowy Prism, żeby obsłużyć każdy czytnik ekranu, i dodaje pomoce nawigacyjne, dostępność menu oraz specjalne sygnały dźwiękowe, które udostępniają minigry.

W obecnym stanie Knights of the Old Republic I jest w pełni grywalny i możliwy do ukończenia, ze wszystkimi minigrami, zadaniami i stylami gry. Można grać na klawiaturze albo na kontrolerze w stylu Xbox — mod ma własną obsługę kontrolerów (patrz sekcja o kontrolerze poniżej); tylko minigry na razie wymagają jeszcze klawiatury.

Trwają prace nad portem części II.

Mod jest przetłumaczony na wszystkie obsługiwane języki — angielski, niemiecki, francuski, włoski, hiszpański — a dodatkowo obsługuje tłumaczenie polskie i rosyjskie. Wspiera wersję 1.03 ze Steama i GoG oraz wersję z 2004 roku, z której korzystają tłumaczenia polskie i rosyjskie.

<h2>Czym są gry Knights of the Old Republic</h2>

Knights of the Old Republic to para gier fabularnych ze świata Gwiezdnych wojen, osadzonych około cztery tysiące lat przed wydarzeniami z filmów. Pierwsza powstała w BioWare w 2003 roku, druga — Knights of the Old Republic II: The Sith Lords — w Obsidianie w 2004 roku i rozgrywa się kilka lat po części I. Obie działają na tym samym silniku i dlatego mod może przenieść się z jednej na drugą.

W obu grach tworzysz własną postać, zbierasz drużynę towarzyszy, podróżujesz między planetami i kształtujesz historię swoimi wyborami w dialogach oraz tym, czy podążasz jasną, czy ciemną stroną Mocy.

Walka działa tak samo w obu: w czasie rzeczywistym z pauzą, oparta na zasadach Star Wars d20 z gry stołowej — ustawiasz akcje w kolejce, a kości je rozstrzygają. Każda runda trwa 6 rzeczywistych sekund i w dowolnym momencie możesz zatrzymać grę, żeby obejrzeć pole bitwy i dodać do kolejki akcje specjalne, znacznie silniejsze niż automatyczne ataki postaci. Na każdą postać możesz ustawić w kolejce do 4 akcji, zanim musisz pozwolić walce się potoczyć, a część akcji możesz anulować, jeśli przebieg walki wymusza zmianę strategii.

<h2>Wymagania</h2>

- Windows 10 lub nowszy
- Star Wars: Knights of the Old Republic, v1.0.3 (Steam lub GoG; dla naszych celów są identyczne bajt po bajcie)
- Czytnik ekranu. Mowa idzie przez Prism, który obsługuje wszystkie aktywnie używane czytniki ekranu; jeśli twój czytnik działa z czymkolwiek innym w systemie, zadziała też z tym modem
- Około 200 MB wolnego miejsca na dysku na środowisko uruchomieniowe patchera, K1 Community Patch i dołączone środowisko mowy

<h3>Wersje gry nieobsługiwane w tym wydaniu</h3>

- Porty Aspyr na urządzenia mobilne / macOS (inny plik wykonywalny)
- Wcześniej zmodyfikowane pliki wykonywalne (zmienione przez UniWS, przez KOTOR High-Resolution Menus)
- Wersje, których suma SHA-256 pliku `swkotor.exe` nie zgadza się z rozpoznawanymi sumami Steam lub GoG 1.0.3

Jeśli instalator zgłosi niezgodność wersji, załóż zgłoszenie i podaj wyświetloną sumę kontrolną. Baza adresów obsługuje Steam i GoG od razu, a dodanie nowej, identycznej bajt po bajcie repaczki to zwykle jedna linia w manifeście.

<h2>Instalacja</h2>

1. Pobierz `VoiceOfTheOldRepublicInstaller.exe` z najnowszego wydania na GitHubie
2. Zamknij KOTOR, jeśli jest uruchomiony
3. Kliknij instalator prawym przyciskiem i wybierz **Uruchom jako administrator**. Przy pierwszym uruchomieniu Windows SmartScreen ostrzeże o „nieznanym wydawcy" — kliknij **Więcej informacji → Uruchom mimo to**. Instalator nie jest jeszcze podpisany cyfrowo, więc to ostrzeżenie jest normalne (sposób weryfikacji pobranego pliku opisano w sekcji Rozwiązywanie problemów)
4. (Zalecane) Zrób kopię folderu zapisów w `%USERPROFILE%\Documents\Swkotor\saves\` przed instalacją, jeśli masz rozpoczętą rozgrywkę
5. Przejdź przez ekrany instalatora. Wykryje twoją instalację KOTOR-a, zainstaluje framework łatek, wdroży mod i domyślnie dołączy K1 Community Patch oraz poprawki panoramicznego obrazu i menu w wysokiej rozdzielczości
6. Uruchom grę z ostatniego ekranu instalatora albo ze Steama

<h2>Odinstalowanie</h2>

Uruchom instalator ponownie i wybierz opcję odinstalowania albo skorzystaj z „Aplikacje i funkcje". Deinstalator usuwa wyłącznie pliki tego moda — K1CP i inne opcjonalne mody wybrane podczas instalacji zostają na miejscu. Aby wrócić do całkowicie czystego KOTOR-a, po odinstalowaniu użyj „Sprawdź spójność plików gry" na Steamie albo zainstaluj grę ponownie z GoG.

<h2>Pierwsze kroki</h2>

Po rozpoczęciu nowej gry KOTOR najpierw prowadzi cię przez **tworzenie postaci**: wybierasz klasę (Żołnierz, Zwiadowca lub Łajdak), portret oraz ustawiasz atrybuty, umiejętności i atuty. Mod odczytuje każdy panel, po którym się poruszasz — nie spiesz się, nic nie jest na czas.

Następnie budzisz się na **Endar Spire**, atakowanym okręcie Republiki. To obszar samouczka. Mod zastępuje ekranowe okienka samouczka własnymi wskazówkami klawiszowymi, napisanymi z myślą o użytkownikach czytników ekranu, dzięki czemu poznajesz sterowanie po drodze. Podążaj za Traskiem, swoim przewodnikiem, w stronę kapsuł ratunkowych.

Kilka nawyków, które znacznie ułatwiają początek gry:

- **Znajduj rzeczy klawiszami Q / E**, a do tego, co już znalazłeś, wracaj cyklem `,` / `.` (patrz skróty klawiszowe poniżej).
- **Naciśnij H** w dowolnym momencie, żeby usłyszeć swoje zdrowie i stan, a **F1**, żeby poznać pełną listę klawiszy.
- **Słuchaj pomieszczenia.** Wejście do pomieszczenia zapowiada jego nazwę, kształt i wyjścia, a cicha warstwa dźwiękowa informuje cię o najbliższych ścianach w trakcie ruchu.
- **Zajrzyj wcześnie do ustawień.** Naciśnij O, żeby otworzyć Opcje gry. W sekcji rozgrywki znajdziesz opcje automatycznej pauzy, które decydują, kiedy walka zatrzymuje się sama — warto ustawić je przed pierwszą prawdziwą walką — oraz przypisania klawiszy.
- **A na samym dole listy Opcji: Ustawienia moda.** Prawie wszystko, co dodaje ten mod, można tam dostosować albo włączyć i wyłączyć, klawisze moda można przypisać na nowo, a słownik dźwięków odtwarza każdy sygnał dźwiękowy z jego nazwą, żebyś nauczył się, co oznacza każdy z nich.

Po Endar Spire docierasz na **Taris**, pierwszy duży świat, i od tego miejsca fabuła się otwiera.

<h2>Skróty klawiszowe</h2>

Mod zostawia domyślny układ klawiszy gry bez zmian, z jednym ergonomicznym wyjątkiem, który instalator wprowadza przy nowej instalacji (patrz uwaga o kroku w bok i obrocie kamery poniżej). Wszystko, czego tu nie wymieniono, działa jak w niezmodyfikowanej grze. Klawisze gry można przypisać na nowo w Opcje → Rozgrywka → Przypisanie klawiszy albo bezpośrednio w `swkotor.ini`. Klawisze dodane przez mod można przypisać na nowo w Ustawienia moda → Przypisania klawiszy.

<h3>Klawisze gry, których użyjesz najczęściej</h3>

- W / S — Ruch do przodu / do tyłu
- A / D — Krok w bok w lewo / w prawo
- Z / C — Obrót kamery w lewo / w prawo (**Y / C** na niemieckiej klawiaturze QWERTZ)
- Q / E — Przełączanie celu w lewo / w prawo
- R — Domyślna akcja na bieżącym celu (atak, rozmowa, otwarcie)
- 1 / 2 / 3 — Użycie trzech akcji z menu akcji bieżącego celu
- 4 / 5 / 6 / 7 — Użycie slotów mocy / apteczki / przedmiotu / miny gracza
- Tab — Zmiana przywódcy drużyny
- F — Przerwanie walki, G — Skradanie, V — Tryb samotny, X — Popis bronią
- Spacja — Pauza
- Esc — Menu gry
- F4 — Szybki zapis, F5 — Szybkie wczytanie (w menu głównym F5 sprawdza natomiast aktualizacje moda)
- I — Ekwipunek drużyny, U — Wyposażenie, P — Karta postaci, K — Umiejętności / atuty / moce
- M — Mapa, L — Zadania, J — Wiadomości, O — Opcje
- Mysz 1 — Kliknięcie w świecie 3D (rzadko potrzebne; patrz tryb widoku poniżej)

> **Krok w bok / obrót kamery:** domyślnie KOTOR używa Z / C do kroku w bok i A / D do obrotu kamery. Instalator dostępności zamienia je przy nowej pełnej instalacji, żeby oba tworzyły wygodną grupę w dolnym rzędzie — **krok w bok na A / D, obrót kamery na Z / C**. Taki układ wymieniono powyżej. Jeśli aktualizowałeś istniejącą instalację albo sam zmienisz przypisania w `swkotor.ini`, twoje klawisze mogą się różnić.

<h3>Klawisze moda — interakcja ze światem</h3>

- Enter — Wykonanie domyślnej akcji na aktualnie zapowiedzianym celu (to samo, co kliknięcie Myszą 1 w świecie)
- Shift+Enter — Otwarcie ujednoliconego menu akcji dla bieżącego celu (wszystkie akcje — atak, rozmowa, moce, przedmioty, zdolności specjalne — w jednym menu)
- Shift+1 … Shift+7 — Otwarcie jednej kategorii akcji do wyboru (1–3 to akcje celu, 4–7 twoje moce / przedmioty / miny)
- H — Zapowiedź własnego zdrowia, aktywnych efektów i założonej broni
- Shift+H — Otwarcie kolejki akcji (przegląd lub wyczyszczenie zakolejkowanych akcji)
- Shift+L — Otwarcie panelu awansu na poziom
- F1 — Otwarcie lub zamknięcie pełnej listy klawiszy; Ctrl+F1 — odczytanie klawiszy bieżącego ekranu
- Ctrl+R — Skopiowanie do schowka ostatnio wypowiedzianego tekstu (kwestii dialogowej, wpisu w dzienniku, imienia NPC — tego, co przeczytano ostatnio)

<h3>Klawisze moda — cykl odkrytych obiektów</h3>

Drugi cykl, obok Q / E, który przechodzi po obiektach już odkrytych w bieżącym obszarze — drzwiach, pojemnikach, postaciach, przejściach między obszarami, punktach orientacyjnych i twoich własnych znacznikach na mapie — pogrupowanych według kategorii. (Włącz „Wybór obiektów na całej mapie" w Ustawieniach moda, żeby objąć też rzeczy jeszcze nieodnalezione.)

- `,` / `.` — Poprzedni / następny obiekt w bieżącej kategorii
- Shift+`,` / Shift+`.` — Poprzednia / następna kategoria (istoty, drzwi, pojemniki, przejścia, znaczniki mapy, …)
- Ctrl+`,` / Ctrl+`.` — Skok do najbliższego / najdalszego obiektu w kategorii
- `/` (układ amerykański) lub `-` (układ niemiecki) — Zapowiedź aktualnie wskazanego obiektu
- Shift+`/` (Shift+`-`) — Automatyczne dojście do tego obiektu
- Ctrl+`/` (Ctrl+`-`) — Włączenie dźwiękowej radiolatarni, która sygnalizuje drogę w trakcie ruchu

<h3>Klawisze moda — orientacja i drużyna</h3>

- AltGr (prawy Alt, samodzielnie) — Zapowiedź bieżącego kierunku patrzenia jako strony świata
- N — Obrót kamery o 90° zgodnie z ruchem wskazówek zegara do następnej strony świata; jeśli radiolatarnia jest włączona, zamiast tego wycelowanie w jej następny punkt trasy
- Tab — Zapowiada nowego przywódcę drużyny po tym, jak silnik przełączy sterowanie

<h3>Klawisze moda — tryb widoku</h3>

Naciśnij B, żeby wejść w tryb widoku. Gdy tryb widoku jest aktywny:

- A / D — Obrót kamery bez ruszania postacią
- Enter — Interakcja z tym, na co wskazuje kamera, albo automatyczne dojście do tego punktu
- Shift+Enter — Otwarcie menu akcji na celu kamery
- Ponownie B — Wyjście z trybu widoku

<h3>Klawisze moda — ekran mapy</h3>

Gdy otwarta jest mapa gry:

- Strzałki / Góra / Dół — Przechodzenie po notatkach i punktach orientacyjnych mapy
- `,` / `.` — Przechodzenie po znacznikach mapy (to samo słownictwo, co w cyklu odkrytych obiektów)
- Shift+N — Postawienie własnego znacznika w bieżącej pozycji kursora w świecie (nazwanego automatycznie od najbliższego pomieszczenia lub punktu orientacyjnego). Nowy znacznik od razu dołącza do cyklu, a Ctrl+`-` ustawia na nim radiolatarnię

<h3>Klawisze moda — podmenu</h3>

Gdy otwarte jest podmenu moda (ujednolicone menu akcji, menu kategorii, kolejka akcji):

- Góra / Dół — Przesuwanie fokusu
- Lewo / Prawo — Przechodzenie między kolumnami lub wariantami
- Enter — Uruchomienie wskazanego wiersza
- Shift+Enter — (tylko kolejka akcji) Wyczyszczenie wszystkich zakolejkowanych akcji
- Esc — Zamknięcie podmenu

<h3>Klawisze moda — zależne od kontekstu</h3>

- Q lub E w panelu pojemnika — Weź wszystko / oddaj przedmioty
- Q lub E w panelu sklepu — Przełączanie między kupnem a sprzedażą

W polu nazwy przy tworzeniu postaci (i w innych polach tekstowych):

- Góra / Dół — Ponowne odczytanie bieżącego tekstu od początku
- Enter — Zatwierdzenie
- Esc — Anulowanie

<h3>Klawisze moda — minigra Pazaak</h3>

Gdy otwarta jest plansza Pazaaka:

- Góra / Dół — Przechodzenie między strefami: twoja ręka, twój stół, stół przeciwnika, akcje (Pas / Koniec tury)
- Lewo / Prawo — Poruszanie się w bieżącej strefie (puste miejsca na ręce są pomijane)
- Enter — Zagranie wskazanej karty z ręki albo uruchomienie wskazanej akcji
- S — Pas
- E — Koniec tury
- C — Odczytanie twojej ręki
- T — Odczytanie obu stołów wraz z sumami
- Shift+C — Ile kart trzyma jeszcze przeciwnik
- Karta plus/minus — Enter otwiera wybór znaku; Lewo / Prawo wybierają plus lub minus, Enter zagrywa z tym znakiem, Esc anuluje

Na ekranie stawki przed partią pierwsza pozycja odczytuje twoją bieżącą stawkę, maksimum stołu i twoje kredyty; przejdź do „Zmniejsz stawkę" / „Zwiększ stawkę" i naciśnij Enter, żeby zmienić stawkę, a potem do przycisku stawki w grze, żeby ją postawić. Edytor talii bocznej odczytuje każdą kartę i każde miejsce w talii.

<h2>Kontroler</h2>

Mod ma własną obsługę kontrolerów — KOTOR 1 nie zawiera żadnego kodu obsługi gamepada, więc to mod steruje padem bezpośrednio. Działa każdy kontroler XInput z układem przycisków Xbox; podłącz go przed uruchomieniem gry. Klawiatura i kontroler pozostają aktywne równolegle, a każda akcja gry wywołana z pada przechodzi przez twoje własne przypisania klawiszy — zmienione przypisania są więc respektowane. Z podłączonym padem lista klawiszy (F1) zyskuje sekcję Kontroler; bez pada ta sekcja znika. Minigry (Pazaak, wyścigi ścigaczy, działko) nie mają jeszcze przypisań na padzie i zachowują swoje klawisze.

Nazwy przycisków poniżej odpowiadają układowi Xbox: A, B, X, Y, lewy i prawy bumper (LB / RB), lewy i prawy spust (LT / RT) oraz wciśnięcie gałki (L3 / R3).

<h3>Ruch i orientacja</h3>

- Lewa gałka — Ruch (wszystkie osiem kierunków: do przodu, do tyłu i krok w bok)
- Prawa gałka — Obrót kamery
- R3 (wciśnięcie prawej gałki) — Obrót kamery do następnego punktu trasy radiolatarni, a bez niej do następnej strony świata (klawiaturowe N)
- Sam prawy spust — Zapowiedź kierunku patrzenia w stopniach

<h3>Obiekty i akcje</h3>

- Krzyżak w lewo / w prawo — Poprzedni / następny odkryty obiekt (klawiaturowy cykl `,` / `.`)
- Krzyżak w górę / w dół — Poprzednia / następna kategoria obiektów
- A — Domyślna akcja na wskazanym celu (atak, otwarcie, rozmowa, podniesienie)
- B — Zamknięcie menu akcji, jeśli jest otwarte; w przeciwnym razie zwykłe anulowanie silnika gry
- Lewy / prawy bumper — Przełączanie celu w lewo / w prawo (klawiaturowe Q / E)
- Sam lewy spust — Otwarcie ujednoliconego menu akcji; ponowne naciśnięcie je zamyka. Gdy jest otwarte: krzyżak w lewo / w prawo zmienia kategorię, w górę / w dół pozycję, A wykonuje wybraną akcję
- Lewy spust + lewy bumper — Radiolatarnia dźwiękowa do wskazanego obiektu
- Prawy spust + prawy bumper — Automatyczne dojście do wskazanego obiektu

<h3>Drużyna, stan i gra</h3>

- X — Zmiana przywódcy drużyny
- Lewy spust + X — Twój stan (klawiaturowe H)
- Prawy spust + X — Kolejka akcji (klawiaturowe Shift+H). W środku: krzyżak w górę / w dół przechodzi po pozycjach, A usuwa ostatnią zakolejkowaną akcję, lewy spust + A czyści całą kolejkę, B zamyka
- Y — Szybkie menu z pozycjami: ekrany menu, przywódca drużyny, tryb samotny, skradanie, szybki zapis i Pomoc, która otwiera listę klawiszy moda. Postać stoi, dopóki menu jest otwarte
- L3 (wciśnięcie lewej gałki) — Popis bronią
- Start — Pauza
- Przycisk Back — Opcje
- Oba spusty (LT + RT) — Odczytanie klawiszy bieżącego ekranu (klawiaturowe Ctrl+F1)

<h3>W menu</h3>

W menu gry pad zachowuje się po prostu jak klawiatura:

- Krzyżak lub lewa gałka — Przesuwanie fokusu
- A — Zatwierdzenie, B — Powrót
- Lewy / prawy bumper — Przechodzenie po podekranach menu gry (wyposażenie, mapa, zadania, …) — tak pad dociera do mapy; w pojemniku i sklepie przełączają natomiast tryb (weź / oddaj, kupno / sprzedaż)
- Przytrzymaj Y i naciśnij krzyżak w górę lub w dół — Odczytanie pełnego opisu wskazanej pozycji, blok po bloku, bez zmiany fokusu
- Na ekranie mapy lewa gałka przesuwa kursor mapy, a krzyżak przechodzi po znacznikach mapy

<h2>Systemy nawigacji w skrócie</h2>

KOTOR to trójwymiarowa gra fabularna, więc większość czasu spędzasz na poruszaniu się po pomieszczeniach i wokół obiektów. Mod nakłada kilka systemów, żebyś zachował orientację — każdy zapowiada się sam w trakcie używania.

<h3>Przełączanie celu — Q / E</h3>

Twój główny sposób na znajdowanie rzeczy i działanie na nich. Q / E przechodzą po istotach, drzwiach i używalnych obiektach widocznych dla kamery; to, co jest wskazane, jest tym, na co działają Enter i klawisze akcji 1–7. Mod zapowiada każdy nowy cel.

<h3>Cykl odkrytych obiektów — `,` / `.`</h3>

Do wracania do rzeczy już znalezionych. `,` / `.` przechodzą po wszystkich obiektach odkrytych w bieżącym obszarze — drzwiach, pojemnikach, postaciach, przejściach, punktach orientacyjnych, twoich własnych znacznikach — pogrupowanych według kategorii. Zapowiedz obiekt, dojdź do niego automatycznie albo włącz dźwiękową radiolatarnię. (Ustawienia moda → „Wybór obiektów na całej mapie" rozszerza cykl także o rzeczy jeszcze nieodnalezione.)

<h3>Ujednolicone menu akcji — Shift+Enter</h3>

Jedno menu ze wszystkimi akcjami dla bieżącego celu — atak, rozmowa, moce, przedmioty, zdolności specjalne. Strzałki przesuwają fokus, Enter uruchamia. Zastępuje osobne menu radialne, menu celu i menu osobiste z gry.

<h3>Mapa — M</h3>

Mapa gry, uczyniona nawigowalną. Przesuwaj kursor strzałkami, żeby odczytywać teren i znaczniki, albo przechodź po znacznikach mapy klawiszami `,` / `.` tym samym słownictwem, co w świecie. Mgła wojny jest respektowana, a Shift+N stawia własny znacznik w miejscu kursora.

<h3>Sygnały ścian i opisy kształtu pomieszczeń</h3>

Kiedy się poruszasz, ciągła warstwa dźwięku 3D odtwarza ciche, przestrzenne kliknięcia odbite od najbliższych ścian — bliższe ściany brzmią głośniej — dzięki czemu stale czujesz przestrzeń wokół siebie. A wejście do pomieszczenia zapowiada jego nazwę, kształt (korytarz, skrzyżowanie, ślepy zaułek, otwarta przestrzeń) i widoczne wyjścia, wszystko liczone na żywo z siatki chodzenia gry.

<h2>Ustawienia moda</h2>

Prawie każdy system dodany przez mod można dostosować albo włączyć i wyłączyć w trakcie gry. Otwórz ekran Opcji gry (O) i wybierz **Ustawienia moda** na samym dole listy. Góra / Dół przechodzą po wierszach, Enter przełącza ustawienie lub otwiera podmenu, Lewo / Prawo przesuwają suwak, a Esc cofa.

- Wybór obiektów na całej mapie — rozszerza cykl `,` / `.` o obiekty jeszcze nieodkryte
- Opisy kształtu pomieszczeń — zapowiedź nazwy, kształtu i wyjść przy wejściu do pomieszczenia
- Dźwięki ścian — ciągła warstwa przestrzennych sygnałów ścian
- Czytaj napisy mówionych kwestii — odczytywanie napisów kwestii z podłożonym głosem
- Automatyczne celowanie — pomoc w celowaniu w minigrze z działkiem
- Pomijaj filmy wstępne — działa od następnego uruchomienia
- Głośność dźwięków podpowiedzi i Głośność komunikatów mówionych — suwaki głośności dla dźwięków i mowy moda
- Przypisania klawiszy — zmiana przypisań klawiszy dodanych przez mod
- Słownik dźwięków — odtwarza każdy sygnał dźwiękowy moda, jeden po drugim wraz z nazwą, żebyś nauczył się, co oznacza każdy dźwięk

<h2>Rozwiązywanie problemów</h2>

<h3>Brak mowy po uruchomieniu gry</h3>

- Upewnij się, że czytnik ekranu działa, zanim uruchomisz KOTOR-a.
- Środowisko mowy Prism jest dołączone do moda i umieszczane automatycznie przez instalator. Przy instalacji ręcznej sprawdź, czy jego pliki są w folderze gry.
- Sprawdź najnowszy dziennik łatki w `<instalacja>\logs\patch-*.log` (przycisk **Zbierz dzienniki** w instalatorze zbiera go za ciebie).

<h3>Gra zamyka się przy starcie albo mod się nie ładuje</h3>

- Uruchom instalator jako administrator — wdraża on pośrednik `dinput8.dll`, który automatycznie ładuje mod przy starcie gry.
- Sprawdź, czy twoja wersja gry jest obsługiwana (patrz „Wersje gry nieobsługiwane" powyżej). Instalator sprawdza sumę kontrolną `swkotor.exe` i poinformuje cię o niezgodności.
- Jeśli gra była niedawno aktualizowana, uruchom instalator ponownie — aktualizacja może nadpisać program ładujący.

<h3>Mod działał, ale przestał po aktualizacji gry</h3>

- Aktualizacje Steama i GoG mogą nadpisać pliki programu ładującego mod. Uruchom instalator ponownie, żeby wdrożyć mod na nowo.

<h3>Skróty klawiszowe nie działają</h3>

- Upewnij się, że okno gry ma fokus (przełącz się do niego Alt+Tab).
- Naciśnij F1. Jeśli słyszysz listę klawiszy, mod jest aktywny.
- Część klawiszy działa tylko w określonym kontekście (klawisze Pazaaka tylko przy planszy Pazaaka, klawisze podmenu tylko wewnątrz podmenu moda i tak dalej).

<h3>Kamera nie obraca się już klawiszami</h3>

- Prawdopodobnie włączyłeś tryb swobodnego rozglądania się. Domyślny układ klawiszy gry podaje Caps Lock jako przełącznik swobodnego rozglądania, więc łatwo trafić w niego przypadkiem — spróbuj nacisnąć Caps Lock ponownie. W przeciwnym razie sprawdź ustawienia myszy i kamery w Opcjach.
- Sprawdź też, czy nie jesteś w trybie widoku moda (B), gdzie A / D obracają kamerę zamiast poruszać postacią. Naciśnij B ponownie, żeby z niego wyjść.

<h3>Zły język</h3>

- Mod dobiera język automatycznie na podstawie języka gry (odczytanego z pliku `dialog.tlk`). W grze nie ma jeszcze przełącznika języka, więc żeby zmienić język moda, zainstaluj grę w tym języku. Obsługiwanych jest pięć języków, w których wydawany jest KOTOR — angielski, francuski, niemiecki, włoski, hiszpański.

<h3>Windows ostrzega, że instalator lub biblioteka DLL są niebezpieczne</h3>

Instalator i mod nie są podpisane cyfrowo. Certyfikaty do podpisywania kodu kosztują setki euro rocznie, co nie jest realne dla darmowego projektu dostępnościowego, więc Windows SmartScreen ostrzeże cię przy pierwszym uruchomieniu instalatora i może oznaczyć pliki jako pochodzące od nieznanego wydawcy.

Aby sprawdzić, czy pobrany plik odpowiada temu opublikowanemu na GitHubie, każde wydanie podaje sumę kontrolną SHA-256. Możesz obliczyć sumę swojego pliku i ją porównać:

- PowerShell: `Get-FileHash <nazwa_pliku> -Algorithm SHA256`
- Wiersz polecenia: `certutil -hashfile <nazwa_pliku> SHA256`

Jeśli suma zgadza się z tą w informacjach o wydaniu, plik jest oryginalny. Aby przejść przez ostrzeżenie SmartScreen, wybierz „Więcej informacji", a następnie „Uruchom mimo to".

<h2>Zgłaszanie błędów</h2>

Ekran po instalacji zawiera przycisk **Zbierz dzienniki**, który pakuje najnowszy dziennik łatki i ewentualne zrzuty Windows Error Reporting do folderu Pobrane. Dołącz to archiwum do [zgłoszenia na GitHubie](https://github.com/JeanStiletto/voice-of-the-old-republic/issues) i opisz, co robiłeś. Jeśli potrafisz odtworzyć awarię, podaj, w jakim obszarze byłeś — zapowiedź pomieszczenia lub obszaru wymieniła go tuż przedtem.

<h2>Znane problemy</h2>

Aktualny stan błędów, planowanych funkcji i niedoróbek znajdziesz w [docs/known-issues.md](known-issues.md).

<h2>Zastrzeżenia</h2>

<h3>Inne rodzaje dostępności</h3>

Na razie jest to mod dostępności dla czytników ekranu. Jestem całkowicie niewidomym programistą i dostęp przez czytnik ekranu to obszar, który znam. Naprawdę chciałbym objąć więcej niepełnosprawności — słabowzroczność, niepełnosprawność ruchową i tak dalej — ale kwestie takie jak kolor, kontrast i krój pisma pozostają dla mnie, osoby całkowicie niewidomej, abstrakcyjne. Jeśli potrzebujesz czegoś w tym kierunku i potrafisz jasno opisać swoje potrzeby oraz pomóc przetestować efekt, odezwij się. Chętnie sprawię, żeby mod zasłużył na swoją nazwę dla większej liczby osób.

<h3>Wykorzystanie sztucznej inteligencji</h3>

Kod tego moda powstaje przy dużym udziale Claude'a od Anthropic, z użyciem modeli Opus (prace objęły generacje Opus 4.5, 4.6 i 4.7). Znam dyskusje wokół programowania wspieranego przez AI. Ale w czasach, gdy branża gier nigdy nie dostarczyła dostępności, której potrzebujemy — ani pod względem jakości, ani ilości — dla tytułów takich jak KOTOR, to właśnie te narzędzia sprawiają, że projekt tej wielkości jest wykonalny dla jednego niewidomego programisty. Każda zmiana jest sprawdzana i testowana w grze, ze słuchu, zanim trafi do wydania.

<h2>Jak współtworzyć</h2>

Wkład jest mile widziany — zwłaszcza poprawki dla języków, konfiguracji systemu lub czytników ekranu, których nie mogę przetestować lokalnie. Przyjmuję też propozycje funkcji. Zanim zaczniesz pracę, przejrzyj plik ze znanymi problemami powyżej, żeby sprawdzić, czy twój pomysł już tam jest.

- Przewodnik dla współtwórców: [CONTRIBUTING.md](../CONTRIBUTING.md)
- Przegląd architektury: [ARCHITECTURE.md](../ARCHITECTURE.md)
- Przewodnik po moddingu dostępności (ogólne rzemiosło + specyfika natywnych plików binarnych): [ACCESSIBILITY_MODDING_GUIDE.md](../ACCESSIBILITY_MODDING_GUIDE.md)
- Tłumaczenie komunikatów mówionych moda: [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md)

<h2>Podziękowania</h2>

A teraz chcę podziękować całej rzeszy osób. Po pierwsze, ten mod opiera się w ogromnym stopniu na pracy społeczności, a wiele osób zrobiło te naprawdę trudne rzeczy, więc ja musiałem tylko sięgnąć po ich narzędzia i stworzyć nimi swoje. Po drugie, na szczęście nie byłem tu sam ze sztuczną inteligencją w czarnej skrzynce, tylko miałem wokół siebie całą sieć ludzi, którzy pomagali, dodawali sił i po prostu byli towarzyscy i mili.

Napisz do mnie, jeśli o kimś zapomniałem albo jeśli chcesz być wymieniony pod innym imieniem lub wcale.

Inżynieria wsteczna i framework łatek, na których działa ten mod, pochodzą od **Lane'a Dibello**, którego [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager) i praca w Ghidrze w ogóle umożliwiły podpięcie się pod grę. Dziękuję też społeczności moddingowej KOTOR-a skupionej wokół **DeadlyStream**, która rozgryzła narzędzia i formaty, po które ja musiałem już tylko sięgnąć.

Wiele osób pomogło mi testować grę, znalazło wszystkie błędy, których sam nie mogłem znaleźć, i dało mi informacje zwrotne, które pozwoliły wskazać słabe i niewygodne miejsca projektu. Bez nich nie powstałby z tego żaden projekt. Dlatego chcę podziękować:

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

**Fundamenty i zależności:**

- **Lane Dibello** — [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager), baza Ghidry uzyskana przez inżynierię wsteczną oraz framework łatek
- **Prism** (Ethin P.) — wieloplatformowy mostek mowy obejmujący wszystkie główne czytniki ekranu, z awaryjnym SAPI
- Zespół **K1 Community Patch** (KOTORCommunityPatches) — dołączona warstwa poprawek błędów
- **xoreos / xoreos-tools** — otwartoźródłowa reimplementacja silnika; punkt odniesienia dla formatów plików
- Społeczność **DeadlyStream** — baza wiedzy o moddingu

<h3>Użyte narzędzia</h3>

- Claude (Anthropic) — partner do programowania w parze w generacjach Opus 4.5, 4.6 i 4.7
- Kotor-Patch-Manager — framework łatek z wstrzykiwaniem biblioteki DLL w czasie działania
- Prism — mostek mowy dla czytników ekranu
- Tolk — biblioteka czytników ekranu (ścieżka awaryjna)
- Ghidra — inżynieria wsteczna
- xoreos-tools — bezokienkowe wypakowywanie formatów plików gry
- K1 Community Patch — dołączona społecznościowa warstwa poprawek błędów

<h2>Wesprzyj swojego moddera</h2>

Tworzenie tego moda było świetną zabawą i prawdziwym źródłem sprawczości, ale kosztowało też mnóstwo czasu i realne pieniądze na subskrypcje Claude'a. Zamierzam je utrzymać, żeby dbać o projekt i rozwijać go przez kolejne lata. Jeśli możesz i chcesz przekazać jednorazową lub cykliczną darowiznę, będę za nią głęboko wdzięczny — docenia ona pracę i daje mi stabilną podstawę, żeby dalej ulepszać Voice of the Old Republic i, mam nadzieję, inne duże projekty dostępnościowe.

[Ko-fi: ko-fi.com/jeanstiletto](https://ko-fi.com/jeanstiletto)

<h2>Licencja</h2>

Kod źródłowy moda jest udostępniony na licencji GNU General Public License v3 (patrz [LICENSE](../LICENSE)). Dołączone zależności w `third_party/` zachowują własne licencje (Prism na MPL-2.0; Tolk na LGPL; Kotor-Patch-Manager dołączony na warunkach projektu źródłowego; dsoal i OpenAL Soft, gdy włączona jest opcjonalna ścieżka dźwięku przestrzennego, na LGPL-2.1). Sama gra i pliki danych BioWare nie są rozpowszechniane przez ten projekt.

<h2>Odnośniki</h2>

- [GitHub](https://github.com/JeanStiletto/voice-of-the-old-republic)
- [Zgłoś problem](https://github.com/JeanStiletto/voice-of-the-old-republic/issues)
- [Kotor-Patch-Manager](https://github.com/LaneDibello/Kotor-Patch-Manager)
- [K1 Community Patch (DeadlyStream)](https://deadlystream.com/)
- [Ko-fi (wesprzyj projekt)](https://ko-fi.com/jeanstiletto)

<h2>Inne języki</h2>

- [English](/voice-of-the-old-republic/)
- [Deutsch](/voice-of-the-old-republic/docs/README.de.html)
- [Français](/voice-of-the-old-republic/docs/README.fr.html)
- [Italiano](/voice-of-the-old-republic/docs/README.it.html)
- [Español](/voice-of-the-old-republic/docs/README.es.html)
- [Русский](/voice-of-the-old-republic/docs/README.ru.html)

Tłumaczenia znajdują się w `docs/README.{de,fr,it,es,pl,ru}.md`. Aby poprawić lub dodać tłumaczenie, zobacz [docs/CONTRIBUTING_TRANSLATIONS.md](CONTRIBUTING_TRANSLATIONS.md).
