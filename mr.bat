@echo off
del osc.exe
del osc.pdb
del osc.res
del osc.obj
@echo on

rc osc.rc
cl /nologo osc.cxx /DNDEBUG /I.\ /DUNICODE /MT /Ox /Qpar /O2 /Oi /Ob2 /EHac /Zi /Gy /D_AMD64_ /link osc.res /OPT:REF /subsystem:windows


