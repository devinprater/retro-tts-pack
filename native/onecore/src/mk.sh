#!/bin/sh
# Build the zf1 test driver from every zf1_*.c except the unit-test mains (zf1_t_*.c).
#   sh mk.sh -> ../build/zf1_main.exe ; OUT=name sh mk.sh -> ../build/name.exe
cd /d/llm-experiments/misk/zira/src
OUT=${OUT:-zf1_main}
mkdir -p ../build/$OUT
SRCS=$(ls zf1_*.c | grep -v '^zf1_t_' | tr '\n' ' ')
cmd //c "D:\\llm-experiments\\misk\\zira\\src\\cl64.bat /nologo /O2 /W3 /Fo..\\build\\$OUT\\ /Fe:..\\build\\$OUT.exe $SRCS" 2>&1 | grep -v "^zf1_[a-z0-9_]*\.c$\|path spec\|^Generating"
