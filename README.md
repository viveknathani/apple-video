# apple-video

Hacking on video stuff for Apple platforms using C.

## system dependencies
1. Make
2. gcc/clang
3. ffmpeg (for video generation and testing)

## usage

Step 1: generate a H264 video to work with

```bash
ffmpeg -f lavfi -t 5 -i testsrc=r=30:s=1920x1080 -c:v h264_videotoolbox video.h264
```

Step 2: run the program
```bash
make build
make run
```

Step 3: get the generated raw video to get back to a viewable format
```bash
ffmpeg -f rawvideo -pix_fmt nv12 -s 1920x1080 -i output.raw -c:v h264_videotoolbox output.mp4
```

### acknowledgements

created by Vivek Nathani ([@viveknathani_](https://twitter.com/viveknathani_)), licensed under the [MIT License](./LICENSE).
