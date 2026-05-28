// Shared speech+log helper for the per-menu Speak* paths
// (target_action_menu, actionbar_menu, radial_menu — same shape: speak a
// label with interrupt, then log it under the menu's tag with a context
// printf string).

#pragma once

namespace acc::menu_speak {

// Speak `label` (interrupt=true) and log it under `tag` with a printf-
// formatted context. Empty/null label is logged as "-> empty" and not
// spoken. The full log line is `<tag> "speak <ctxFmt-result> [<label>]"`
// or `<tag> "speak <ctxFmt-result> -> empty"`.
void SpeakChoice(const char* tag, const char* label,
                 const char* ctxFmt, ...);

}  // namespace acc::menu_speak
