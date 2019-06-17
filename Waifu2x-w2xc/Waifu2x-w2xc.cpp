/*
  MIT License

  Copyright (c) 2018-2019 HolyWu

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <cmath>
#include <string>

#include <VapourSynth.h>
#include <VSHelper.h>

#include <w2xconv.h>

struct Waifu2xData {
    VSNodeRef * node;
    VSVideoInfo vi;
    int noise, scale, block;
    int iterTimesTwiceScaling;
    float * srcInterleaved, * dstInterleaved;
    W2XConv * conv;
};

static bool isPowerOf2(const int i) noexcept {
    return i && !(i & (i - 1));
}

static bool filter(const VSFrameRef * src, VSFrameRef * dst, Waifu2xData * const VS_RESTRICT d, const VSAPI * vsapi) noexcept {
    const int width = vsapi->getFrameWidth(src, 0);
    const int height = vsapi->getFrameHeight(src, 0);
    const int srcStride = vsapi->getStride(src, 0) / sizeof(float);
    const int dstStride = vsapi->getStride(dst, 0) / sizeof(float);
    const float * srcpR = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 0));
    const float * srcpG = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 1));
    const float * srcpB = reinterpret_cast<const float *>(vsapi->getReadPtr(src, 2));
    float * VS_RESTRICT dstpR = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 0));
    float * VS_RESTRICT dstpG = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 1));
    float * VS_RESTRICT dstpB = reinterpret_cast<float *>(vsapi->getWritePtr(dst, 2));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int pos = (width * y + x) * 3;
            d->srcInterleaved[pos + 0] = srcpR[x];
            d->srcInterleaved[pos + 1] = srcpG[x];
            d->srcInterleaved[pos + 2] = srcpB[x];
        }

        srcpR += srcStride;
        srcpG += srcStride;
        srcpB += srcStride;
    }

    if (w2xconv_convert_rgb_f32(d->conv,
                                reinterpret_cast<unsigned char *>(d->dstInterleaved),
                                d->vi.width * 3 * sizeof(float),
                                reinterpret_cast<unsigned char *>(d->srcInterleaved),
                                width * 3 * sizeof(float),
                                width, height, d->noise, d->scale, d->block) < 0)
        return false;

    for (int y = 0; y < d->vi.height; y++) {
        for (int x = 0; x < d->vi.width; x++) {
            const int pos = (d->vi.width * y + x) * 3;
            dstpR[x] = d->dstInterleaved[pos + 0];
            dstpG[x] = d->dstInterleaved[pos + 1];
            dstpB[x] = d->dstInterleaved[pos + 2];
        }

        dstpR += dstStride;
        dstpG += dstStride;
        dstpB += dstStride;
    }

    return true;
}

static void VS_CC waifu2xInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData * d = static_cast<Waifu2xData *>(*instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC waifu2xGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData * d = static_cast<Waifu2xData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef * dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);

        if (!filter(src, dst, d, vsapi)) {
            char * error = w2xconv_strerror(&d->conv->last_error);
            vsapi->setFilterError((std::string{ "Waifu2x-w2xc: " } + error).c_str(), frameCtx);
            w2xconv_free(error);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            return nullptr;
        }

        vsapi->freeFrame(src);
        return dst;
    }

    return nullptr;
}

static void VS_CC waifu2xFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData * d = static_cast<Waifu2xData *>(instanceData);

    vsapi->freeNode(d->node);

    delete[] d->srcInterleaved;
    delete[] d->dstInterleaved;

    w2xconv_fini(d->conv);

    delete d;
}

static void VS_CC waifu2xCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData d{};
    int err;

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);

    try {
        if (!isConstantFormat(&d.vi) || d.vi.format->colorFamily != cmRGB || d.vi.format->sampleType != stFloat || d.vi.format->bitsPerSample != 32)
            throw std::string{ "only constant RGB format and 32 bit float input supported" };

        d.noise = int64ToIntS(vsapi->propGetInt(in, "noise", 0, &err));

        d.scale = int64ToIntS(vsapi->propGetInt(in, "scale", 0, &err));
        if (err)
            d.scale = 2;

        d.block = int64ToIntS(vsapi->propGetInt(in, "block", 0, &err));
        if (err)
            d.block = 512;

        const bool photo = !!vsapi->propGetInt(in, "photo", 0, &err);

        W2XConvGPUMode gpu = static_cast<W2XConvGPUMode>(int64ToIntS(vsapi->propGetInt(in, "gpu", 0, &err)));
        if (err)
            gpu = W2XCONV_GPU_AUTO;

        int processor = int64ToIntS(vsapi->propGetInt(in, "processor", 0, &err));
        if (err)
            processor = -1;

        const bool log = !!vsapi->propGetInt(in, "log", 0, &err);

        size_t numProcessors;
        const W2XConvProcessor * processors = w2xconv_get_processor_list(&numProcessors);

        if (d.noise < -1 || d.noise > 3)
            throw std::string{ "noise must be -1, 0, 1, 2, or 3" };

        if (d.scale < 1 || !isPowerOf2(d.scale))
            throw std::string{ "scale must be greater than or equal to 1 and be a power of 2" };

        if (d.block < 1)
            throw std::string{ "block must be greater than or equal to 1" };

        if (gpu < 0 || gpu > 2)
            throw std::string{ "gpu must be 0, 1, or 2" };

        if (processor >= static_cast<int>(numProcessors))
            throw std::string{ "the specified processor is not available" };

        if (!!vsapi->propGetInt(in, "list_proc", 0, &err)) {
            std::string text;

            for (size_t i = 0; i < numProcessors; i++) {
                const W2XConvProcessor * p = &processors[i];
                const char * type;

                switch (p->type) {
                case W2XCONV_PROC_HOST:
                    switch (p->sub_type) {
                    case W2XCONV_PROC_HOST_FMA:
                        type = "FMA";
                        break;
                    case W2XCONV_PROC_HOST_AVX:
                        type = "AVX";
                        break;
                    case W2XCONV_PROC_HOST_SSE3:
                        type = "SSE3";
                        break;
                    default:
                        type = "OpenCV";
                    }
                    break;

                case W2XCONV_PROC_CUDA:
                    type = "CUDA";
                    break;

                case W2XCONV_PROC_OPENCL:
                    type = "OpenCL";
                    break;

                default:
                    type = "unknown";
                }

                text += std::to_string(i) + ": " + p->dev_name + " (" + type + ")\n";
            }

            VSMap * args = vsapi->createMap();
            vsapi->propSetNode(args, "clip", d.node, paReplace);
            vsapi->freeNode(d.node);
            vsapi->propSetData(args, "text", text.c_str(), -1, paReplace);

            VSMap * ret = vsapi->invoke(vsapi->getPluginById("com.vapoursynth.text", core), "Text", args);
            if (vsapi->getError(ret)) {
                vsapi->setError(out, vsapi->getError(ret));
                vsapi->freeMap(args);
                vsapi->freeMap(ret);
                return;
            }

            d.node = vsapi->propGetNode(ret, "clip", 0, nullptr);
            vsapi->freeMap(args);
            vsapi->freeMap(ret);
            vsapi->propSetNode(out, "clip", d.node, paReplace);
            vsapi->freeNode(d.node);
            return;
        }

        if (d.noise == -1 && d.scale == 1) {
            vsapi->propSetNode(out, "clip", d.node, paReplace);
            vsapi->freeNode(d.node);
            return;
        }

        if (d.scale != 1) {
            d.vi.width *= d.scale;
            d.vi.height *= d.scale;
            d.iterTimesTwiceScaling = static_cast<int>(std::log2(d.scale));
        }

        d.srcInterleaved = new (std::nothrow) float[vsapi->getVideoInfo(d.node)->width * vsapi->getVideoInfo(d.node)->height * 3];
        d.dstInterleaved = new (std::nothrow) float[d.vi.width * d.vi.height * 3];
        if (!d.srcInterleaved || !d.dstInterleaved)
            throw std::string{ "malloc failure (srcInterleaved/dstInterleaved)" };

        const int numThreads = vsapi->getCoreInfo(core)->numThreads;
        if (processor > -1)
            d.conv = w2xconv_init_with_processor(processor, numThreads, log);
        else
            d.conv = w2xconv_init(gpu, numThreads, log);

        const std::string pluginPath{ vsapi->getPluginPath(vsapi->getPluginById("com.holywu.waifu2x-w2xc", core)) };
        std::string modelPath{ pluginPath.substr(0, pluginPath.find_last_of('/')) };
        if (photo)
            modelPath += "/models/photo";
        else
            modelPath += "/models/anime_style_art_rgb";

        if (w2xconv_load_models(d.conv, modelPath.c_str()) < 0) {
            char * error = w2xconv_strerror(&d.conv->last_error);
            vsapi->setError(out, (std::string{ "Waifu2x-w2xc: " } + error).c_str());
            w2xconv_free(error);
            vsapi->freeNode(d.node);
            w2xconv_fini(d.conv);
            return;
        }
    } catch (const std::string & error) {
        vsapi->setError(out, ("Waifu2x-w2xc: " + error).c_str());
        vsapi->freeNode(d.node);
        return;
    }

    Waifu2xData * data = new Waifu2xData{ d };

    vsapi->createFilter(in, out, "Waifu2x-w2xc", waifu2xInit, waifu2xGetFrame, waifu2xFree, fmParallelRequests, 0, data, core);
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.waifu2x-w2xc", "w2xc", "Image Super-Resolution using Deep Convolutional Neural Networks", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Waifu2x",
                 "clip:clip;"
                 "noise:int:opt;"
                 "scale:int:opt;"
                 "block:int:opt;"
                 "photo:int:opt;"
                 "gpu:int:opt;"
                 "processor:int:opt;"
                 "list_proc:int:opt;"
                 "log:int:opt;",
                 waifu2xCreate, nullptr, plugin);
}
