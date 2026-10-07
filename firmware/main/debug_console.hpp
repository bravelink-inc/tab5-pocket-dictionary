#pragma once
// Debug console on the USB-C serial port (USB Serial/JTAG):
//   * typed characters / arrow keys are injected as key events (for testing without a keyboard)
//   * 'S' requests a screenshot: the UI task then dumps the frame buffer as base64 (see tools/screenshot.py)
void debug_console_start();
bool debug_screenshot_pending();
void debug_dump_screen();
