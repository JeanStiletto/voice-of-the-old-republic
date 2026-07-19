#pragma once

// Scoped special-case narration for the Endar Spire command-module scripted
// "spectator battle" (module END_M01AA): Republic soldiers make a doomed last
// stand against the Sith beyond a walkmesh gap the player cannot cross (soldier
// tags end_cut2_soldier*). A sighted player watches the firefight through the
// open doorway and grasps "press on, you can't help"; a blind player only got
// the bare name plus a generic "way blocked", which read as an ally they
// inexplicably couldn't walk to (patch-20260718-225737.log analysis).
//
// Deliberately hard-scoped to this one encounter by creature tag. When more
// spectator battles surface elsewhere in the game we generalise this into a
// real "combat among unreachable non-party creatures" detector; until then a
// tag whitelist keeps false positives at zero.
namespace acc::spectator {

// True when `obj` is one of this scene's Republic soldiers (tag prefix
// "end_cut2_soldier", case-insensitive). Safe on any client/server object;
// false on a read fault or when the object has no tag.
bool IsScriptedBattleSoldier(void* obj);

// First-sight funnel hook — called from passive_narrate::NarrateHandle for
// every narrated object. The first time a scene soldier is narrated in the
// current area visit, queues the dramatic line after the name the funnel just
// spoke. No-op for every other object and on repeat sightings this visit.
void OnObjectNarrated(void* obj);

// The dramatic in-world line, localised (Id::SpectatorBattleDoomed). Spoken on
// first sight (once) and again each time a walk toward a scene soldier is
// cancelled as "way blocked".
const char* DramaticLine();

}  // namespace acc::spectator
