#pragma once

// ---------------------------------------------------------------------------
// Which renderer the drawing tests run through.
//
// The reference images are Direct2D output, so Windows renders natively and is
// the anchor: it re-proves the references still describe what Direct2D draws.
// Everywhere else the tests use the SHARED CPU backend, which reproduces
// Direct2D's own rasterisation (gmpi_ui backends/CpuGfx.h plus
// helpers/CpuTextEngine.h) and so is measured against the same references
// rather than against a per-platform set. One set of references, one expected
// result, on every platform - which is the point.
//
// Set GMPI_UI_TESTS_NATIVE_BACKEND=1 (here, or -DGMPI_UI_TESTS_NATIVE_BACKEND=ON
// at configure time) to put macOS back on the Cocoa/CoreText backend. That is
// the pre-2026-08 behaviour, and it fails ~55 of the image tests, because
// CoreText's glyph rasterisation is not Direct2D's - keep it for diagnosing the
// Cocoa backend itself, not for a green run.
//
// DELIBERATELY free of #includes. The implementation files test these macros
// BEFORE including anything else, because "Drawing.h" is an ambiguous name -
// gmpi_ui has one and SynthEditLib/modules/se_sdk3 has another, and which one
// wins depends on the consumer's include paths. Each backend header pulls in
// the right one by relative path, so the guarded body must come first and the
// includes second.
// ---------------------------------------------------------------------------
#ifndef GMPI_UI_TESTS_NATIVE_BACKEND
#define GMPI_UI_TESTS_NATIVE_BACKEND 0
#endif

// Resolved once, so the implementation files cannot disagree about which of
// them is the live one.
#if defined(_WIN32)
    #define GMPI_UI_TESTS_BACKEND_CPU 0                        // always Direct2D: it defines the references
#elif defined(__APPLE__)
    #define GMPI_UI_TESTS_BACKEND_CPU (!GMPI_UI_TESTS_NATIVE_BACKEND)
#else
    #define GMPI_UI_TESTS_BACKEND_CPU 1                        // no native backend on Linux
#endif
