#include "key_inject.h"

#include <windows.h>

#pragma comment(lib, "user32.lib")

namespace acc::key_inject {

void Send(int scancode, bool down, bool extended) {
    if (scancode == 0) return;
    INPUT inp = {};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wScan   = static_cast<WORD>(scancode & 0xFF);
    inp.ki.dwFlags = KEYEVENTF_SCANCODE;
    if (extended) inp.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
    if (!down)    inp.ki.dwFlags |= KEYEVENTF_KEYUP;
    SendInput(1, &inp, sizeof(INPUT));
}

void Tap(int scancode, bool extended) {
    Send(scancode, /*down=*/true,  extended);
    Send(scancode, /*down=*/false, extended);
}

}  // namespace acc::key_inject
