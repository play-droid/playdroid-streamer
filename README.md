To start
```
./playdroid-streamer -w 800 -y 800 -l "appsrc name=src is-live=true format=time ! vaapipostproc  !  vaapih264enc bitrate=512  ! h264parse ! queue ! matroskamux ! queue leaky=2 ! tcpserversink port=5001 host=0.0.0.0 recover-policy=keyframe sync-method=latest-keyframe "
```

and for test on other terminal:
```
./test_server
```

tcp://localhost:5001 should play something 
