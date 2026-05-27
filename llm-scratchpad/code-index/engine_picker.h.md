# engine_picker.h (77 lines)

Engine action picker — drives the game's context-sensitive action dispatcher for an arbitrary target without moving the cursor. Documents the 4-step mechanism (SetMainInterfaceTarget, GetDefaultActions, click-gate write, HandleMouseClickInWorld) and CSWGuiInterfaceAction layout at +0x4c8.

## Declarations (in source order)

- L38 — `namespace acc::picker`
- L41 — `struct ActionSnapshot`
  note: snapshot of +0x4c8 descriptor after GetDefaultActions; radial_opened true means count==0 or forceRadial caused PopulateMenus to open the radial instead
- L54 — `bool Drive(uint32_t targetServerHandle, ActionSnapshot* outSnapshot, bool forceRadial = false)`
  note: ORs 0x80000000 onto server handle before writing engine slots; forceRadial bypasses default-action dispatch (Shift+Enter semantics)
- L75 — `bool ReadCurrent(ActionSnapshot* outSnapshot)`
  note: reads +0x4c8 without driving anything — observe engine's own picker for diagnostics
