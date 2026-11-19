// Leaks are not the bug class this integration fuzzes for: turn LeakSanitizer off at build time while
// ASan and UBSan stay fully active.
extern "C" int __lsan_is_turned_off() { return 1; }
