# cameraserver
Photobooth software (Multiplatform, but linux version currently unable to stream video feed properly). Support multiple concurrent cameras (DSLR, webcam, ipcam) and multiple client (browser based). The idea behind this is that you only need one mini pc/laptop for multiple photobooths.
![desc1](https://user-images.githubusercontent.com/64301921/126728803-010e898d-3c5f-4eec-9da7-1beb88fec06a.jpg)

Supported effects and filters :
1. Greenscreen background removal
2. Dynamic background changes using multimedia file
3. Picture Frame
4. Smart sticker utilizing AI for eye tracking
5. Some filters
6. Multiple Dynamic foreground using multimedia file

![Screenshot_20210723-093359_Chrome](https://user-images.githubusercontent.com/64301921/126732671-2ff9a2a5-247d-496b-a32a-f34c607e785a.jpg)
![Screenshot_20210723-093426_Chrome](https://user-images.githubusercontent.com/64301921/126732677-6de13c3f-409d-49b7-bed3-7c32ba246f6e.jpg)
![Screenshot_20210723-093527_Chrome](https://user-images.githubusercontent.com/64301921/126732690-7e51421d-91de-44ad-8d39-f0fe7dadbd02.jpg)

Requirements:
1. OpenCV 4 + (can be obtained via vcpkg)
2. Libwebsockets 3.1
3. SDL 2 (can be obtained via vcpkg)
4. Canon EDSDK (non free, currently disabled)
5. FFMPEG (can be obtained via vcpkg)
6. RapidJson
7. Dirent

Webcam DEMO

<iframe width="560" height="315" src="https://www.youtube.com/embed/ybRvi3fbMg0" title="YouTube video player" frameborder="0" allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture" allowfullscreen></iframe>
