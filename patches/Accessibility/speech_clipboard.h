// Copy-the-last-spoken-text hotkey (Ctrl+R).
//
// Speech is fire-and-forget: once a line has been read there is no way to get
// it out of the game. That hurts most on the number-heavy computer-terminal
// riddles, where the player has to hold several figures in working memory, but
// it applies just as much to a long journal entry, an awkwardly spelled NPC or
// planet name, or a quest description worth pasting into notes.
//
// Ctrl+R puts prism::LastSpoken() on the Windows clipboard as CF_UNICODETEXT and
// speaks a short confirmation. Deliberately NOT dialog-specific: whatever the
// screen reader said last is what gets copied, so the same key works in a
// conversation, in the journal, on a character sheet, and in the world.
//
// Win32 polling rather than an engine hook: the key has to work on every
// screen, including ones where the engine never routes R to us.
//
// The clipboard write itself runs on a short-lived worker thread and reports
// back to the next tick, which then speaks the confirmation — the engine's
// main thread is an STA running the message loop and DirectInput dispatch,
// and clipboard calls there wake system-wide listeners. See the RunCopy
// comment block in the .cpp.

#pragma once

namespace acc::speech_clipboard {

void PollWin32();

}  // namespace acc::speech_clipboard
