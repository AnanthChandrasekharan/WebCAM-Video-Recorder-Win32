del VideoCapture.exe output.*
cl.exe VideoCapture.cpp /EHsc /link mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib
del VideoCapture.obj
VideoCapture.exe
