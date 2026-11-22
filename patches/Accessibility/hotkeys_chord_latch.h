// Whose press is it when a chord's key reaches the engine after the modifier
// is up again? The engine reads buffered, modifier-blind presses; the mod reads
// the live keyboard. A quick Shift+L can have Shift released by the time the
// engine's L arrives, and the engine then opens its own screen for bare L on
// top of the mod's action.
//
// The latch pairs the two views of one physical press: the poll notes a chord
// going down, the input hook notes an engine press it swallowed with the
// modifier still held, and whichever comes second finds the first. Short-lived,
// so it can never take a later bare press. Engine-free.
//
// Ported from aradix85's fork (issue 12).

#pragma once

#include <cstdint>

namespace acc::hotkeys {

class ChordLatch {
public:
    // The poll saw a modified combo on `vk` go down. Nothing to wait for when
    // the engine's press of it was already swallowed.
    void ChordDown(int vk, uint64_t nowMs) {
        if (Fresh(m_swallowedVk, m_swallowedMs, vk, nowMs)) {
            m_swallowedVk = 0;
            return;
        }
        m_chordVk = vk;
        m_chordMs = nowMs;
    }

    // The input hook swallowed the engine's press of `vk` under a held modifier.
    void EngineSwallowed(int vk, uint64_t nowMs) {
        if (Fresh(m_chordVk, m_chordMs, vk, nowMs)) {
            m_chordVk = 0;
            return;
        }
        m_swallowedVk = vk;
        m_swallowedMs = nowMs;
    }

    // The engine's press of `vk` arrives with no modifier held: true, once, when
    // it belongs to a chord the poll just saw.
    bool TakeEnginePress(int vk, uint64_t nowMs) {
        if (!Fresh(m_chordVk, m_chordMs, vk, nowMs)) return false;
        m_chordVk = 0;
        return true;
    }

private:
    static constexpr uint64_t kWindowMs = 500;

    static bool Fresh(int notedVk, uint64_t notedMs, int vk, uint64_t nowMs) {
        return vk != 0 && notedVk == vk && nowMs - notedMs < kWindowMs;
    }

    int      m_chordVk     = 0;
    uint64_t m_chordMs     = 0;
    int      m_swallowedVk = 0;
    uint64_t m_swallowedMs = 0;
};

}  // namespace acc::hotkeys
