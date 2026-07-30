// Engine struct/vtable offset table. Pure constants — no behaviour.
// Values from Lane's Ghidra DB; GoG bytes match Steam. File-scope (not
// namespaced) for callsite brevity, same as engine_input.h's kInput*.
//
// This is now a thin aggregator. The 358 constants were split 2026-07-29 by
// what makes them change (refactoring phase 2, candidate C8):
//
//   engine_offsets_types.h      C++ struct layouts (CExoArrayList, Vector,
//                               CExoString).
//   engine_offsets_addresses.h  .text functions and vtables, every one wrapped
//                               in acc::addr::R(), plus the raw .data global
//                               pointers.
//   engine_offsets_fields.h     Struct field offsets, plus the geometry and
//                               bit-mask constants that decode them.
//   engine_offsets_values.h     Vtable slot indices, TLK strrefs, sentinels,
//                               enum bytes, panel input codes.
//
// Including this header gets you all four, so nothing changed for existing
// callers. Include a narrower one when you only need that class of constant.
// The rationale, and what each group costs on a KOTOR 2 port, is in
// engine_offsets_types.h and archiev/refactoring/reports/phase-2-cleanup.md.

#pragma once

#include "engine_offsets_addresses.h"
#include "engine_offsets_fields.h"
#include "engine_offsets_types.h"
#include "engine_offsets_values.h"
