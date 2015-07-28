/*
  The MIT License (MIT)

  Copyright (c) 2015 HolyWu

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
#include <vapoursynth/VapourSynth.h>
#include <vapoursynth/VSHelper.h>
#include <w2xconv.h>

struct Waifu2xData {
    VSNodeRef * node;
    VSVideoInfo vi;
    int noise, scale, block;
    bool photo;
    W2XConvGPUMode gpu;
    int iterTimesTwiceScaling;
};

static inline bool isPowerOf2(const int i) {
    return i && !(i & (i - 1));
}

static bool Waifu2x(const VSFrameRef * src, VSFrameRef * dst, float * VS_RESTRICT srcInterleaved, float * VS_RESTRICT dstInterleaved, float * VS_RESTRICT buffer,
                    W2XConv * conv, const Waifu2xData * d, const VSAPI * vsapi) {
    if (d->vi.format->colorFamily == cmRGB) {
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
                const int pos = y * width + x;
                srcInterleaved[pos * 3] = srcpR[x];
                srcInterleaved[pos * 3 + 1] = srcpG[x];
                srcInterleaved[pos * 3 + 2] = srcpB[x];
            }
            srcpR += srcStride;
            srcpG += srcStride;
            srcpB += srcStride;
        }

        if (w2xconv_convert_rgb_f32(conv, reinterpret_cast<unsigned char *>(dstInterleaved), d->vi.width * 3 * sizeof(float),
                                    reinterpret_cast<unsigned char *>(srcInterleaved), width * 3 * sizeof(float), width, height, d->noise, d->scale, d->block) < 0)
            return false;

        for (int y = 0; y < d->vi.height; y++) {
            for (int x = 0; x < d->vi.width; x++) {
                const int pos = y * d->vi.width + x;
                dstpR[x] = dstInterleaved[pos * 3];
                dstpG[x] = dstInterleaved[pos * 3 + 1];
                dstpB[x] = dstInterleaved[pos * 3 + 2];
            }
            dstpR += dstStride;
            dstpG += dstStride;
            dstpB += dstStride;
        }
    } else {
        for (int plane = 0; plane < d->vi.format->numPlanes; plane++) {
            const int width = vsapi->getFrameWidth(src, plane);
            const int height = vsapi->getFrameHeight(src, plane);
            const int srcStride = vsapi->getStride(src, plane) / sizeof(float);
            const int dstStride = vsapi->getStride(dst, plane) / sizeof(float);
            const float * srcp = reinterpret_cast<const float *>(vsapi->getReadPtr(src, plane));
            float * VS_RESTRICT dstp = reinterpret_cast<float *>(vsapi->getWritePtr(dst, plane));

            if (plane != 0) {
                const float * input = srcp;

                if (d->noise == 0) {
                    float * VS_RESTRICT output = dstp;
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++)
                            output[x] = input[x] + 0.5f;
                        input += srcStride;
                        output += dstStride;
                    }
                } else {
                    float * VS_RESTRICT output = buffer;
                    for (int y = 0; y < height; y++) {
                        for (int x = 0; x < width; x++)
                            output[x] = input[x] + 0.5f;
                        input += srcStride;
                        output += width;
                    }
                }
            }

            if (d->noise != 0) {
                if (plane == 0) {
                    if (w2xconv_apply_filter_y(conv, static_cast<W2XConvFilterType>(d->noise - 1), reinterpret_cast<unsigned char *>(dstp), vsapi->getStride(dst, plane),
                                               const_cast<unsigned char *>(reinterpret_cast<const unsigned char *>(srcp)), vsapi->getStride(src, plane), width, height, d->block) < 0)
                        return false;
                } else {
                    if (w2xconv_apply_filter_y(conv, static_cast<W2XConvFilterType>(d->noise - 1), reinterpret_cast<unsigned char *>(dstp), vsapi->getStride(dst, plane),
                                               reinterpret_cast<unsigned char *>(buffer), width * sizeof(float), width, height, d->block) < 0)
                        return false;
                }
            }

            if (d->scale != 1) {
                if (d->noise == 0 && plane == 0)
                    vs_bitblt(dstp, vsapi->getStride(dst, plane), srcp, vsapi->getStride(src, plane), width * sizeof(float), height);

                for (int n = 0; n < d->iterTimesTwiceScaling; n++) {
                    const int currentWidth = width << n;
                    const int currentHeight = height << n;
                    const int currentWidth2 = currentWidth * 2;
                    const int currentHeight2 = currentHeight * 2;
                    const float * input = dstp;

                    for (int y = 0; y < currentHeight; y++) {
                        for (int x = 0; x < currentWidth; x++) {
                            const int pos = y * 2 * currentWidth2 + x * 2;
                            buffer[pos] = input[x];
                            buffer[pos + 1] = input[x];
                            buffer[pos + currentWidth2] = input[x];
                            buffer[pos + currentWidth2 + 1] = input[x];
                        }
                        input += dstStride;
                    }

                    if (w2xconv_apply_filter_y(conv, W2XCONV_FILTER_SCALE2x, reinterpret_cast<unsigned char *>(dstp), vsapi->getStride(dst, plane),
                                               reinterpret_cast<unsigned char *>(buffer), currentWidth2 * sizeof(float), currentWidth2, currentHeight2, d->block) < 0)
                        return false;
                }
            }

            if (plane != 0) {
                const int dstWidth = vsapi->getFrameWidth(dst, plane);
                const int dstHeight = vsapi->getFrameHeight(dst, plane);

                for (int y = 0; y < dstHeight; y++) {
                    for (int x = 0; x < dstWidth; x++)
                        dstp[x] -= 0.5f;
                    dstp += dstStride;
                }
            }
        }
    }

    return true;
}

static void VS_CC waifu2xInit(VSMap *in, VSMap *out, void **instanceData, VSNode *node, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData * d = static_cast<Waifu2xData *>(*instanceData);
    vsapi->setVideoInfo(&d->vi, 1, node);
}

static const VSFrameRef *VS_CC waifu2xGetFrame(int n, int activationReason, void **instanceData, void **frameData, VSFrameContext *frameCtx, VSCore *core, const VSAPI *vsapi) {
    const Waifu2xData * d = static_cast<const Waifu2xData *>(*instanceData);

    if (activationReason == arInitial) {
        vsapi->requestFrameFilter(n, d->node, frameCtx);
    } else if (activationReason == arAllFramesReady) {
        const VSFrameRef * src = vsapi->getFrameFilter(n, d->node, frameCtx);
        VSFrameRef * dst = vsapi->newVideoFrame(d->vi.format, d->vi.width, d->vi.height, src, core);

        float * srcInterleaved = nullptr, * dstInterleaved = nullptr, * buffer = nullptr;
        if (d->vi.format->colorFamily == cmRGB) {
            srcInterleaved = vs_aligned_malloc<float>(vsapi->getFrameWidth(src, 0) * vsapi->getFrameHeight(src, 0) * 3 * sizeof(float), 32);
            dstInterleaved = vs_aligned_malloc<float>(d->vi.width * d->vi.height * 3 * sizeof(float), 32);
            if (!srcInterleaved || !dstInterleaved) {
                vsapi->setFilterError("Waifu2x: malloc failure (srcInterleaved/dstInterleaved)", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                return nullptr;
            }
        } else {
            buffer = vs_aligned_malloc<float>(d->vi.width * d->vi.height * sizeof(float), 32);
            if (!buffer) {
                vsapi->setFilterError("Waifu2x: malloc failure (buffer)", frameCtx);
                vsapi->freeFrame(src);
                vsapi->freeFrame(dst);
                return nullptr;
            }
        }

        std::string pluginPath(vsapi->getPluginPath(vsapi->getPluginById("com.holywu.waifu2x", core)));
        pluginPath = pluginPath.substr(0, pluginPath.find_last_of('/'));
        if (d->vi.format->colorFamily == cmRGB) {
            if (d->photo)
                pluginPath = pluginPath.append("/models/ukbench");
            else
                pluginPath = pluginPath.append("/models/anime_style_art_rgb");
        } else {
            pluginPath = pluginPath.append("/models/anime_style_art");
        }

        W2XConv * conv = w2xconv_init(d->gpu, 0, 0);
        if (w2xconv_load_models(conv, pluginPath.c_str()) < 0) {
            char * err = w2xconv_strerror(&conv->last_error);
            vsapi->setFilterError(std::string("Waifu2x: ").append(err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            w2xconv_free(err);
            w2xconv_fini(conv);
            return nullptr;
        }

        if (!Waifu2x(src, dst, srcInterleaved, dstInterleaved, buffer, conv, d, vsapi)) {
            char * err = w2xconv_strerror(&conv->last_error);
            vsapi->setFilterError(std::string("Waifu2x: ").append(err).c_str(), frameCtx);
            vsapi->freeFrame(src);
            vsapi->freeFrame(dst);
            w2xconv_free(err);
            w2xconv_fini(conv);
            return nullptr;
        }

        vsapi->freeFrame(src);
        vs_aligned_free(srcInterleaved);
        vs_aligned_free(dstInterleaved);
        vs_aligned_free(buffer);
        w2xconv_fini(conv);
        return dst;
    }

    return nullptr;
}

static void VS_CC waifu2xFree(void *instanceData, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData * d = static_cast<Waifu2xData *>(instanceData);
    vsapi->freeNode(d->node);
    delete d;
}

static void VS_CC waifu2xCreate(const VSMap *in, VSMap *out, void *userData, VSCore *core, const VSAPI *vsapi) {
    Waifu2xData d;
    int err;

    d.noise = int64ToIntS(vsapi->propGetInt(in, "noise", 0, &err));
    if (err)
        d.noise = 1;
    d.scale = int64ToIntS(vsapi->propGetInt(in, "scale", 0, &err));
    if (err)
        d.scale = 2;
    d.block = int64ToIntS(vsapi->propGetInt(in, "block", 0, &err));
    if (err)
        d.block = 512;
    d.photo = !!vsapi->propGetInt(in, "photo", 0, &err);
    d.gpu = static_cast<W2XConvGPUMode>(vsapi->propGetInt(in, "gpu", 0, &err));
    if (err)
        d.gpu = W2XCONV_GPU_AUTO;

    if (d.noise < 0 || d.noise > 2) {
        vsapi->setError(out, "Waifu2x: noise must be set to 0, 1 or 2");
        return;
    }
    if (d.scale < 1 || !isPowerOf2(d.scale)) {
        vsapi->setError(out, "Waifu2x: scale must be greater than or equal to 1 and be a power of 2");
        return;
    }
    if (d.block < 1) {
        vsapi->setError(out, "Waifu2x: block must be greater than or equal to 1");
        return;
    }

    d.node = vsapi->propGetNode(in, "clip", 0, nullptr);
    d.vi = *vsapi->getVideoInfo(d.node);

    if (d.noise == 0 && d.scale == 1) {
        vsapi->propSetNode(out, "clip", d.node, paReplace);
        vsapi->freeNode(d.node);
        return;
    }

    if (!isConstantFormat(&d.vi) || d.vi.format->sampleType != stFloat || d.vi.format->bitsPerSample != 32) {
        vsapi->setError(out, "Waifu2x: only constant format 32-bit float input supported");
        vsapi->freeNode(d.node);
        return;
    }

    if (d.scale != 1 && d.vi.format->subSamplingW > 2) {
        vsapi->setError(out, "Waifu2x: horizontal subsampling larger than 2 is not supported");
        vsapi->freeNode(d.node);
        return;
    }

    VSPlugin * fmtcPlugin = vsapi->getPluginById("fmtconv", core);
    if (d.scale != 1 && d.vi.format->subSamplingW != 0 && !fmtcPlugin) {
        vsapi->setError(out, "Waifu2x: the fmtconv plugin is required for fixing horizontal chroma shift");
        return;
    }

    if (d.scale != 1) {
        d.vi.width *= d.scale;
        d.vi.height *= d.scale;
        d.iterTimesTwiceScaling = static_cast<int>(std::log2(d.scale));
    }

    Waifu2xData * data = new Waifu2xData(d);

    vsapi->createFilter(in, out, "Waifu2x", waifu2xInit, waifu2xGetFrame, waifu2xFree, fmParallelRequests, 0, data, core);

    if (d.scale != 1 && d.vi.format->subSamplingW != 0) {
        const double offset = (d.vi.format->subSamplingW == 1) ? 0.5 : 1.5;
        double shift = 0.;
        for (int n = 0; n < d.iterTimesTwiceScaling; n++)
            shift = shift * 2. + offset;

        VSNodeRef * node = vsapi->propGetNode(out, "clip", 0, nullptr);
        vsapi->clearMap(out);
        VSMap * args = vsapi->createMap();
        vsapi->propSetNode(args, "clip", node, paReplace);
        vsapi->freeNode(node);
        vsapi->propSetFloat(args, "sx", shift, paReplace);
        vsapi->propSetFloat(args, "planes", 2, paReplace);
        vsapi->propSetFloat(args, "planes", 3, paAppend);
        vsapi->propSetFloat(args, "planes", 3, paAppend);

        VSMap * ret = vsapi->invoke(fmtcPlugin, "resample", args);
        if (vsapi->getError(ret)) {
            vsapi->setError(out, vsapi->getError(ret));
            vsapi->freeMap(args);
            vsapi->freeMap(ret);
            return;
        }

        node = vsapi->propGetNode(ret, "clip", 0, nullptr);
        vsapi->freeMap(args);
        vsapi->freeMap(ret);
        vsapi->propSetNode(out, "clip", node, paReplace);
        vsapi->freeNode(node);
    }
}

//////////////////////////////////////////
// Init

VS_EXTERNAL_API(void) VapourSynthPluginInit(VSConfigPlugin configFunc, VSRegisterFunction registerFunc, VSPlugin *plugin) {
    configFunc("com.holywu.waifu2x", "w2xc", "Image Super-Resolution using Deep Convolutional Neural Networks", VAPOURSYNTH_API_VERSION, 1, plugin);
    registerFunc("Waifu2x", "clip:clip;noise:int:opt;scale:int:opt;block:int:opt;photo:int:opt;gpu:int:opt;", waifu2xCreate, nullptr, plugin);
}
