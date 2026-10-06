# Writes the vita_heap_census C++ code from src/main.cpp (MKXP_VITA_OBJ_HIST block) to stdout.
import sys
s = open(sys.argv[1]).read()
a = s.index(' * vita_heap_census (MKXP_VITA_OBJ_HIST)')
a = s.rindex('#ifdef MKXP_VITA_OBJ_HIST', 0, a)
b = s.index('#endif\n#ifdef MKXP_VITA_ERROR_SCREEN', a)
print(s[a + len('#ifdef MKXP_VITA_OBJ_HIST'):b])
