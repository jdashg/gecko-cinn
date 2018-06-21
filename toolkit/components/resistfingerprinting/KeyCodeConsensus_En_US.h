/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

/**
 * This file contains the spoofed keycodes of en-US for fingerprinting resistance.
 * When privacy.resistFingerprinting is active, we spoof the user's keyboard
 * layout according to the language of the document.
 *
 * Use CONTROL to define the control key.
 *   CONTROL(keyNameIndex, codeNameIndex, keyCode)
 *   @param keyNameIndex  The keyNameIndex of this control key.
 *                        See KeyNameList.h for details.
 *   @param codeNameIndex The codeNameIndex of this contorl key.
 *                        See PhysicalKeyCodeNameList.h for details.
 *   @param keyCode       The keyCode of this control key.
 *                        See KeyEvent.webidl for details.
 *
 * Use KEY to define the key with its modifier states. The key will be spoofed
 * with given modifier states.
 *   KEY(keyString, codeNameIndex, keyCode, modifiers)
 *   @param keyString     The key string of this key.
 *   @param codeNameIndex The codeNameIndex of this key.
 *                        See PhysicalKeyCodeNameList.h for details.
 *   @param keyCode       The keyCode of this key.
 *                        See KeyEvent.webidl for details.
 *   @param modifiers     The spoofing modifier states for this key.
 *                        See BasicEvents.h for details.
 */

/**
 * Spoofed keycodes for English content (US English keyboard layout).
 */

CONTROL(Alt,         AltLeft,     dom::bindings::KeyboardEvent::DOM_VK_ALT)
CONTROL(ArrowDown,   ArrowDown,   dom::bindings::KeyboardEvent::DOM_VK_DOWN)
CONTROL(ArrowLeft,   ArrowLeft,   dom::bindings::KeyboardEvent::DOM_VK_LEFT)
CONTROL(ArrowRight,  ArrowRight,  dom::bindings::KeyboardEvent::DOM_VK_RIGHT)
CONTROL(ArrowUp,     ArrowUp,     dom::bindings::KeyboardEvent::DOM_VK_UP)
CONTROL(Backspace,   Backspace,   dom::bindings::KeyboardEvent::DOM_VK_BACK_SPACE)
CONTROL(CapsLock,    CapsLock,    dom::bindings::KeyboardEvent::DOM_VK_CAPS_LOCK)
// Leaving "ContextMenu" key unimplemented; not every english keyboard has this.
// For example, MACOS doesn't have this.
CONTROL(Control,     ControlLeft, dom::bindings::KeyboardEvent::DOM_VK_CONTROL)
CONTROL(Delete,      Delete,      dom::bindings::KeyboardEvent::DOM_VK_DELETE)
CONTROL(End,         End,         dom::bindings::KeyboardEvent::DOM_VK_END)
CONTROL(Enter,       Enter,       dom::bindings::KeyboardEvent::DOM_VK_RETURN)
CONTROL(Escape,      Escape,      dom::bindings::KeyboardEvent::DOM_VK_ESCAPE)
// Leaving "Help" key unimplemented; it only appears in some keyboard in Linux.
CONTROL(Home,        Home,        dom::bindings::KeyboardEvent::DOM_VK_HOME)
CONTROL(Insert,      Insert,      dom::bindings::KeyboardEvent::DOM_VK_INSERT)
CONTROL(Meta,        OSLeft,      dom::bindings::KeyboardEvent::DOM_VK_WIN)
CONTROL(OS,          OSLeft,      dom::bindings::KeyboardEvent::DOM_VK_WIN)
CONTROL(PageDown,    PageDown,    dom::bindings::KeyboardEvent::DOM_VK_PAGE_DOWN)
CONTROL(PageUp,      PageUp,      dom::bindings::KeyboardEvent::DOM_VK_PAGE_UP)
// Leaving "Pause", "PrintScreen" and "ScrollLock" keys unimplemented; they are
// non-MACOS only.
CONTROL(Shift,       ShiftLeft,   dom::bindings::KeyboardEvent::DOM_VK_SHIFT)
CONTROL(Tab,         Tab,         dom::bindings::KeyboardEvent::DOM_VK_TAB)
CONTROL(F1,          F1,          dom::bindings::KeyboardEvent::DOM_VK_F1)
CONTROL(F2,          F2,          dom::bindings::KeyboardEvent::DOM_VK_F2)
CONTROL(F3,          F3,          dom::bindings::KeyboardEvent::DOM_VK_F3)
CONTROL(F4,          F4,          dom::bindings::KeyboardEvent::DOM_VK_F4)
CONTROL(F5,          F5,          dom::bindings::KeyboardEvent::DOM_VK_F5)
CONTROL(F6,          F6,          dom::bindings::KeyboardEvent::DOM_VK_F6)
CONTROL(F7,          F7,          dom::bindings::KeyboardEvent::DOM_VK_F7)
CONTROL(F8,          F8,          dom::bindings::KeyboardEvent::DOM_VK_F8)
CONTROL(F9,          F9,          dom::bindings::KeyboardEvent::DOM_VK_F9)
CONTROL(F10,         F10,         dom::bindings::KeyboardEvent::DOM_VK_F10)
CONTROL(F11,         F11,         dom::bindings::KeyboardEvent::DOM_VK_F11)
CONTROL(F12,         F12,         dom::bindings::KeyboardEvent::DOM_VK_F12)
// Leaving "F13" to "F35" key unimplemented; they are some how platform dependent.
// "F13" to "F19" are on MAC's full keyboard but may not exist on usual keyboard.
// "F20" to "F24" are only available on Windows and Linux.
// "F25" to "F35" are Linux only.
// Leaving "Clear" key unimplemented; it's inconsistent between platforms.
KEY(" ",  Space,        dom::bindings::KeyboardEvent::DOM_VK_SPACE,  MODIFIER_NONE)
KEY(",",  Comma,        dom::bindings::KeyboardEvent::DOM_VK_COMMA, MODIFIER_NONE)
KEY("<",  Comma,        dom::bindings::KeyboardEvent::DOM_VK_COMMA, MODIFIER_SHIFT)
KEY(".",  Period,       dom::bindings::KeyboardEvent::DOM_VK_PERIOD, MODIFIER_NONE)
KEY(">",  Period,       dom::bindings::KeyboardEvent::DOM_VK_PERIOD, MODIFIER_SHIFT)
KEY("/",  Slash,        dom::bindings::KeyboardEvent::DOM_VK_SLASH, MODIFIER_NONE)
KEY("?",  Slash,        dom::bindings::KeyboardEvent::DOM_VK_SLASH, MODIFIER_SHIFT)
KEY(";",  Semicolon,    dom::bindings::KeyboardEvent::DOM_VK_SEMICOLON,  MODIFIER_NONE)
KEY(":",  Semicolon,    dom::bindings::KeyboardEvent::DOM_VK_SEMICOLON,  MODIFIER_SHIFT)
KEY("'",  Quote,        dom::bindings::KeyboardEvent::DOM_VK_QUOTE, MODIFIER_NONE)
KEY("\"", Quote,        dom::bindings::KeyboardEvent::DOM_VK_QUOTE, MODIFIER_SHIFT)
KEY("[",  BracketLeft,  dom::bindings::KeyboardEvent::DOM_VK_OPEN_BRACKET, MODIFIER_NONE)
KEY("{",  BracketLeft,  dom::bindings::KeyboardEvent::DOM_VK_OPEN_BRACKET, MODIFIER_SHIFT)
KEY("]",  BracketRight, dom::bindings::KeyboardEvent::DOM_VK_CLOSE_BRACKET, MODIFIER_NONE)
KEY("}",  BracketRight, dom::bindings::KeyboardEvent::DOM_VK_CLOSE_BRACKET, MODIFIER_SHIFT)
KEY("`",  Backquote,    dom::bindings::KeyboardEvent::DOM_VK_BACK_QUOTE, MODIFIER_NONE)
KEY("~",  Backquote,    dom::bindings::KeyboardEvent::DOM_VK_BACK_QUOTE, MODIFIER_SHIFT)
KEY("\\", Backslash,    dom::bindings::KeyboardEvent::DOM_VK_BACK_SLASH, MODIFIER_NONE)
KEY("|",  Backslash,    dom::bindings::KeyboardEvent::DOM_VK_BACK_SLASH, MODIFIER_SHIFT)
KEY("-",  Minus,        dom::bindings::KeyboardEvent::DOM_VK_HYPHEN_MINUS, MODIFIER_NONE)
KEY("_",  Minus,        dom::bindings::KeyboardEvent::DOM_VK_HYPHEN_MINUS, MODIFIER_SHIFT)
KEY("=",  Equal,        dom::bindings::KeyboardEvent::DOM_VK_EQUALS, MODIFIER_NONE)
KEY("+",  Equal,        dom::bindings::KeyboardEvent::DOM_VK_EQUALS, MODIFIER_SHIFT)
KEY("A",  KeyA,         dom::bindings::KeyboardEvent::DOM_VK_A, MODIFIER_SHIFT)
KEY("B",  KeyB,         dom::bindings::KeyboardEvent::DOM_VK_B, MODIFIER_SHIFT)
KEY("C",  KeyC,         dom::bindings::KeyboardEvent::DOM_VK_C, MODIFIER_SHIFT)
KEY("D",  KeyD,         dom::bindings::KeyboardEvent::DOM_VK_D, MODIFIER_SHIFT)
KEY("E",  KeyE,         dom::bindings::KeyboardEvent::DOM_VK_E, MODIFIER_SHIFT)
KEY("F",  KeyF,         dom::bindings::KeyboardEvent::DOM_VK_F, MODIFIER_SHIFT)
KEY("G",  KeyG,         dom::bindings::KeyboardEvent::DOM_VK_G, MODIFIER_SHIFT)
KEY("H",  KeyH,         dom::bindings::KeyboardEvent::DOM_VK_H, MODIFIER_SHIFT)
KEY("I",  KeyI,         dom::bindings::KeyboardEvent::DOM_VK_I, MODIFIER_SHIFT)
KEY("J",  KeyJ,         dom::bindings::KeyboardEvent::DOM_VK_J, MODIFIER_SHIFT)
KEY("K",  KeyK,         dom::bindings::KeyboardEvent::DOM_VK_K, MODIFIER_SHIFT)
KEY("L",  KeyL,         dom::bindings::KeyboardEvent::DOM_VK_L, MODIFIER_SHIFT)
KEY("M",  KeyM,         dom::bindings::KeyboardEvent::DOM_VK_M, MODIFIER_SHIFT)
KEY("N",  KeyN,         dom::bindings::KeyboardEvent::DOM_VK_N, MODIFIER_SHIFT)
KEY("O",  KeyO,         dom::bindings::KeyboardEvent::DOM_VK_O, MODIFIER_SHIFT)
KEY("P",  KeyP,         dom::bindings::KeyboardEvent::DOM_VK_P, MODIFIER_SHIFT)
KEY("Q",  KeyQ,         dom::bindings::KeyboardEvent::DOM_VK_Q, MODIFIER_SHIFT)
KEY("R",  KeyR,         dom::bindings::KeyboardEvent::DOM_VK_R, MODIFIER_SHIFT)
KEY("S",  KeyS,         dom::bindings::KeyboardEvent::DOM_VK_S, MODIFIER_SHIFT)
KEY("T",  KeyT,         dom::bindings::KeyboardEvent::DOM_VK_T, MODIFIER_SHIFT)
KEY("U",  KeyU,         dom::bindings::KeyboardEvent::DOM_VK_U, MODIFIER_SHIFT)
KEY("V",  KeyV,         dom::bindings::KeyboardEvent::DOM_VK_V, MODIFIER_SHIFT)
KEY("W",  KeyW,         dom::bindings::KeyboardEvent::DOM_VK_W, MODIFIER_SHIFT)
KEY("X",  KeyX,         dom::bindings::KeyboardEvent::DOM_VK_X, MODIFIER_SHIFT)
KEY("Y",  KeyY,         dom::bindings::KeyboardEvent::DOM_VK_Y, MODIFIER_SHIFT)
KEY("Z",  KeyZ,         dom::bindings::KeyboardEvent::DOM_VK_Z, MODIFIER_SHIFT)
KEY("a",  KeyA,         dom::bindings::KeyboardEvent::DOM_VK_A, MODIFIER_NONE)
KEY("b",  KeyB,         dom::bindings::KeyboardEvent::DOM_VK_B, MODIFIER_NONE)
KEY("c",  KeyC,         dom::bindings::KeyboardEvent::DOM_VK_C, MODIFIER_NONE)
KEY("d",  KeyD,         dom::bindings::KeyboardEvent::DOM_VK_D, MODIFIER_NONE)
KEY("e",  KeyE,         dom::bindings::KeyboardEvent::DOM_VK_E, MODIFIER_NONE)
KEY("f",  KeyF,         dom::bindings::KeyboardEvent::DOM_VK_F, MODIFIER_NONE)
KEY("g",  KeyG,         dom::bindings::KeyboardEvent::DOM_VK_G, MODIFIER_NONE)
KEY("h",  KeyH,         dom::bindings::KeyboardEvent::DOM_VK_H, MODIFIER_NONE)
KEY("i",  KeyI,         dom::bindings::KeyboardEvent::DOM_VK_I, MODIFIER_NONE)
KEY("j",  KeyJ,         dom::bindings::KeyboardEvent::DOM_VK_J, MODIFIER_NONE)
KEY("k",  KeyK,         dom::bindings::KeyboardEvent::DOM_VK_K, MODIFIER_NONE)
KEY("l",  KeyL,         dom::bindings::KeyboardEvent::DOM_VK_L, MODIFIER_NONE)
KEY("m",  KeyM,         dom::bindings::KeyboardEvent::DOM_VK_M, MODIFIER_NONE)
KEY("n",  KeyN,         dom::bindings::KeyboardEvent::DOM_VK_N, MODIFIER_NONE)
KEY("o",  KeyO,         dom::bindings::KeyboardEvent::DOM_VK_O, MODIFIER_NONE)
KEY("p",  KeyP,         dom::bindings::KeyboardEvent::DOM_VK_P, MODIFIER_NONE)
KEY("q",  KeyQ,         dom::bindings::KeyboardEvent::DOM_VK_Q, MODIFIER_NONE)
KEY("r",  KeyR,         dom::bindings::KeyboardEvent::DOM_VK_R, MODIFIER_NONE)
KEY("s",  KeyS,         dom::bindings::KeyboardEvent::DOM_VK_S, MODIFIER_NONE)
KEY("t",  KeyT,         dom::bindings::KeyboardEvent::DOM_VK_T, MODIFIER_NONE)
KEY("u",  KeyU,         dom::bindings::KeyboardEvent::DOM_VK_U, MODIFIER_NONE)
KEY("v",  KeyV,         dom::bindings::KeyboardEvent::DOM_VK_V, MODIFIER_NONE)
KEY("w",  KeyW,         dom::bindings::KeyboardEvent::DOM_VK_W, MODIFIER_NONE)
KEY("x",  KeyX,         dom::bindings::KeyboardEvent::DOM_VK_X, MODIFIER_NONE)
KEY("y",  KeyY,         dom::bindings::KeyboardEvent::DOM_VK_Y, MODIFIER_NONE)
KEY("z",  KeyZ,         dom::bindings::KeyboardEvent::DOM_VK_Z, MODIFIER_NONE)
KEY("0",  Digit0,       dom::bindings::KeyboardEvent::DOM_VK_0, MODIFIER_NONE)
KEY("1",  Digit1,       dom::bindings::KeyboardEvent::DOM_VK_1, MODIFIER_NONE)
KEY("2",  Digit2,       dom::bindings::KeyboardEvent::DOM_VK_2, MODIFIER_NONE)
KEY("3",  Digit3,       dom::bindings::KeyboardEvent::DOM_VK_3, MODIFIER_NONE)
KEY("4",  Digit4,       dom::bindings::KeyboardEvent::DOM_VK_4, MODIFIER_NONE)
KEY("5",  Digit5,       dom::bindings::KeyboardEvent::DOM_VK_5, MODIFIER_NONE)
KEY("6",  Digit6,       dom::bindings::KeyboardEvent::DOM_VK_6, MODIFIER_NONE)
KEY("7",  Digit7,       dom::bindings::KeyboardEvent::DOM_VK_7, MODIFIER_NONE)
KEY("8",  Digit8,       dom::bindings::KeyboardEvent::DOM_VK_8, MODIFIER_NONE)
KEY("9",  Digit9,       dom::bindings::KeyboardEvent::DOM_VK_9, MODIFIER_NONE)
KEY(")",  Digit0,       dom::bindings::KeyboardEvent::DOM_VK_0, MODIFIER_SHIFT)
KEY("!",  Digit1,       dom::bindings::KeyboardEvent::DOM_VK_1, MODIFIER_SHIFT)
KEY("@",  Digit2,       dom::bindings::KeyboardEvent::DOM_VK_2, MODIFIER_SHIFT)
KEY("#",  Digit3,       dom::bindings::KeyboardEvent::DOM_VK_3, MODIFIER_SHIFT)
KEY("$",  Digit4,       dom::bindings::KeyboardEvent::DOM_VK_4, MODIFIER_SHIFT)
KEY("%",  Digit5,       dom::bindings::KeyboardEvent::DOM_VK_5, MODIFIER_SHIFT)
KEY("^",  Digit6,       dom::bindings::KeyboardEvent::DOM_VK_6, MODIFIER_SHIFT)
KEY("&",  Digit7,       dom::bindings::KeyboardEvent::DOM_VK_7, MODIFIER_SHIFT)
KEY("*",  Digit8,       dom::bindings::KeyboardEvent::DOM_VK_8, MODIFIER_SHIFT)
KEY("(",  Digit9,       dom::bindings::KeyboardEvent::DOM_VK_9, MODIFIER_SHIFT)
