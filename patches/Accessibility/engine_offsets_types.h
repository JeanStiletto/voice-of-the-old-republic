// Engine C++ type definitions shared across the patch.
//
// Part of the engine_offsets.h family, split 2026-07-29 (refactoring phase 2,
// candidate C8) along the axis that decides portability rather than along
// subsystem lines. The four files and what varies them:
//
//   engine_offsets_types.h      THIS FILE. Plain C++ struct layouts for engine
//                               types we read through or construct. No
//                               addresses, no offsets.
//   engine_offsets_addresses.h  Executable addresses: .text functions and
//                               vtables (all through acc::addr::R) plus the raw
//                               .data global pointers. Upstream AddressDatabase
//                               tables `functions` and `global_pointers`.
//   engine_offsets_fields.h     Struct field offsets. Upstream AddressDatabase
//                               table `offsets`, keyed class + member.
//   engine_offsets_values.h     Constants that are neither: vtable slot
//                               indices, TLK strrefs, sentinels, enum bytes,
//                               panel input codes. Not in the address database
//                               at all.
//
// engine_offsets.h remains a thin aggregator of all four, so every existing
// includer is unaffected.
//
// Why this axis: on a KOTOR 2 port each group moves independently. Every
// address is wrong. Struct offsets are mostly wrong but not entirely -
// upstream's seeded K2 database already carries CAppManager|Client at the same
// 0x4 we use. Resource-derived values do not track the executable at all; they
// follow dialog.tlk and the .gui files. Splitting them apart makes the port's
// real risk surface countable. See
// archiev/refactoring/reports/phase-2-cleanup.md.

#pragma once

#include <cstdint>

// CExoArrayList<T*> — engine container for arrays of pointers.
struct CExoArrayList {
    void** data;
    int    size;
    int    capacity;
};

// Vector — Aurora-engine 3D vector. Used for world position, object
// orientation (heading vector with z=0), audio listener pose, etc.
//
// Coordinate frame (investigation Q1 — right-handed, Z-up):
//   +X = east, +Y = north, +Z = up. 1.0 unit ≈ 1 metre.
//   Bearing convention for object orientation: 0° = +X = east; CCW positive.
struct Vector {
    float x;
    float y;
    float z;
};

// CExoString - the engine's string type: heap c_string plus explicit length.
// Most engine string accessors construct one in place into caller-supplied
// storage; see the CTlkTable / CSWFeat / CSWSItem entries in
// engine_offsets_addresses.h for the ownership rules that go with that.
struct CExoString {
    char*    c_string;
    uint32_t length;
};
