Description
===========

waifu2x filter for VapourSynth, based on the w2xc library.


Note
====

The folder `models` must be located in the same folder as `Waifu2x-w2xc.dll`.

The filter will generate .bin files at the same location of the model files for the first time it runs. Make sure that your executable has write permission to the folder of the model files. The filter still can run even without the .bin files being generated, but the performance will degrade. When you update the model files in the future, remember to delete the .bin files as well.


Usage
=====

    w2xc.Waifu2x(clip clip[, int noise=0, int scale=2, int block=512, bint photo=False, int gpu=1, int processor=-1, bint list_proc=False, bint log=False])

* clip: Clip to process. Only planar RGB format with float sample type of 32 bit depth is supported.

* noise: Noise reduction level.
  * -1 = none
  * 0 = low
  * 1 = medium
  * 2 = high
  * 3 = highest

* scale: Upscaling factor. Must be a power of 2. Set to 1 for no upscaling.

* block: The block size for dividing the image during processing. Smaller value results in lower VRAM usage, while larger value may give faster speed. The optimal value may vary according to different graphics card and image size.

* photo: When set to false, it uses the anime-style model. When set to true, the photo model will be used instead.

* gpu: Controls the environment to use.
  * 0 = disable GPU
  * 1 = auto detect. It will run on the first available environment in the following order:
    * CUDA
    * AMD OpenCL
    * FMA
    * AVX
    * Intel OpenCL
    * SSE3
    * OpenCV filter2D
  * 2 = force to use OpenCL

* processor: Sets target processor. Any non-negative value supersedes the choice from `gpu`. Use `list_proc` to get the index of the available processors.

* list_proc: Whether to draw the processors list on the frame.

* log: Whether to write the internal processing stats to stdout.


Compilation
===========

Requires [w2xc](https://github.com/DeadSix27/waifu2x-converter-cpp).

```
meson build
ninja -C build
ninja -C build install
```
