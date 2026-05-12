// Swift bridge declarations for iOS touch input
// This header allows Swift to call C functions for input events

#ifndef IOSTOUCH_INPUT_BRIDGE_H
#define IOSTOUCH_INPUT_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize touch controller (call once at app startup)
void IOSTouchInput_Initialize(void);

// Button state changes (use VPAD button flag values)
void IOSTouchInput_ButtonPressed(uint64_t buttonMask);
void IOSTouchInput_ButtonReleased(uint64_t buttonMask);

// Analog stick input (x, y from -1.0 to 1.0)
void IOSTouchInput_SetLeftStick(float x, float y);
void IOSTouchInput_SetRightStick(float x, float y);

// Clear all buttons and sticks
void IOSTouchInput_ClearAllButtons(void);

#ifdef __cplusplus
}
#endif

#endif // IOSTOUCH_INPUT_BRIDGE_H
