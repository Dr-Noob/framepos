# framepos

Find the position of one or more frames inside a video

### Usage
To search for a given frame or image inside a video, the frame has to be in RAW format. The usual scenario is a video which is encoded in YUV420p colorspace. To search the position of a image in the video, the image must be a RAW YUV420p image. To get this image, the tool `cutframe.sh` can be very useful.

#### cutframe.sh

To know the exact frame to obtain the image we want, a possible way to do it is to use `mpv`. It can be configured to [print the current frame](https://github.com/mpv-player/mpv/issues/3428). With Ctrl+O, seek to the desired frame and obtain the frame number. Then, cut it with `cutframe`. For example, `./cutframe vid.mkv 1000 img.yuv` will obtain the frame in the position 1000 of the input video and will store it as `img.yuv` (RAW YUV420p image). One can visualize which frame has just cut using the script `yuv_to_png.sh` (e.g, `./yuv_to_png.sh img.yuv img.png`)

#### framepos

With the image (or images) ready, framepos will search for the image inside the video. The usage can be checked in the executable itself. The position of the frames are printed to stderr and the output  format is intented so it can be interpreted by ffmpeg to cut the video. The following example shows how to use `framepos` with ffmpeg easily:

```
[noob@drnoob ~]$ ./framepos --video test.mkv --image img1.yuv --image img2.yuv 2> out.txt
[noob@drnoob ~]$ cat out.txt
[IMG 0]: 00:10:12.07
[IMG 1]: 00:20:44.08
[noob@drnoob ~]$ start=$(cat out.txt | grep 'IMG 0' | cut -d' ' -f3)
[noob@drnoob ~]$ end=$(cat out.txt | grep 'IMG 1' | cut -d' ' -f3)
[noob@drnoob ~]$ ffmpeg -ss $start -to $end -i test.mkv -async 1 cut.mkv
```
