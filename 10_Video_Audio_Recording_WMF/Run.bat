del VideoCapture.exe output.*
cl.exe /EHsc /MD /Fe:VideoCapture.exe VideoCapture.cpp /link mfplat.lib mf.lib mfreadwrite.lib mfuuid.lib ole32.lib
del VideoCapture.obj
VideoCapture.exe
