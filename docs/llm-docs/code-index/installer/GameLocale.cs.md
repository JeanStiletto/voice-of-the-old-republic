# GameLocale.cs (124 lines)

Defines the `GameLocale` enum and a detector that reads a game install's language straight from the `dialog.tlk` header (bytes 8-11 = language ID, LE uint32) plus a content probe for Russian community translations, which re-encode the English (ID 0) slot in Windows-1251 rather than claiming a real BioWare language ID. Content probe always outranks the declared ID. Threshold constants must stay in lockstep with `TlkLooksCyrillic` in patches/Accessibility/core_dllmain.cpp — a mismatch would give the installer and the in-game DLL different opinions about the install's language. Consumed by ModSelectionForm (Russian-specific K1CP footnote) and MainForm-adjacent flow.

## Declarations (in source order)

- L8 — `public enum GameLocale` — Unknown=-1, English=0, French=1, German=2, Italian=3, Spanish=4, Russian=100
  note: Russian sits outside the 0..4 ID range deliberately — it's a detection result, never a raw tlk language-id cast
- L33 — `public static class GameLocaleDetector`
- L39 — `CyrillicSampleBytes` = 64KB, `CyrillicThreshold` = 0.20
- L42 — `static GameLocale Detect(string gameDir)` — reads 20-byte tlk header, checks Cyrillic content probe first, then falls back to the language-ID switch
- L102 — `static bool LooksCyrillic(FileStream fs, uint blobOffset)` — samples string blob, ratio of bytes >= 0xC0; measured 77.8% on Russian vs 1.3% on German (umlauts)
