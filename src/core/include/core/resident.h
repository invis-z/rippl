#pragma once

#ifdef RIPPL_RESIDENT_TABLES
#define RIPPL_RESIDENT __attribute__((section(".resident")))
#define RIPPL_CODE2 __attribute__((section(".code2"), noinline))
#else
#define RIPPL_RESIDENT
#define RIPPL_CODE2
#endif

#ifdef RIPPL_NP_FACES_SECTIONS
#define RIPPL_NP_FACES_CODE __attribute__((section(".np_faces.text")))
#define RIPPL_NP_FACES_DATA __attribute__((section(".np_faces.rodata")))
#define RIPPL_NP_FACES_ENTRY __attribute__((section(".np_faces.entry"), used))
#else
#define RIPPL_NP_FACES_CODE
#define RIPPL_NP_FACES_DATA
#define RIPPL_NP_FACES_ENTRY
#endif

#ifdef RIPPL_STARTUP_SECTIONS
#define RIPPL_STARTUP_CODE __attribute__((section(".startup.text"), noinline))
#define RIPPL_STARTUP_ENTRY __attribute__((section(".startup.entry"), used))
#define RIPPL_STARTUP_DATA __attribute__((section(".startup.rodata")))
#define RIPPL_STARTUP_HELPER __attribute__((section(".startup.text")))
#else
#define RIPPL_STARTUP_CODE
#define RIPPL_STARTUP_ENTRY
#define RIPPL_STARTUP_DATA
#define RIPPL_STARTUP_HELPER
#endif

#ifdef RIPPL_ENGINE_SECTIONS
#define RIPPL_ENGINE_CODE __attribute__((section(".engine.text")))
#define RIPPL_ENGINE_ENTRY __attribute__((section(".engine.entry"), used))
#else
#define RIPPL_ENGINE_CODE
#define RIPPL_ENGINE_ENTRY
#endif
