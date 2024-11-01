del Window.exe
cl /EHsc /DUNICODE Window.cpp /link ole32.lib user32.lib gdi32.lib
del Window.obj
Window.exe
