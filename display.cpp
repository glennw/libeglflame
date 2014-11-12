#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <assert.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>
#include <hardware/hwcomposer.h>
#include "hardware/power.h"

#include "utils/RefBase.h"
#include <gui/Surface.h>
#include <gui/GraphicBufferAlloc.h>
#include "FramebufferSurface.h"

using namespace android;

class Display {

public:
    Display(void **egl_native_window)
    {
        hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &gralloc_module);

        hw_get_module(HWC_HARDWARE_MODULE_ID, &hwc_module);
        hwc_open_1(hwc_module, &hwc);

        int32_t values[2];
        const uint32_t attrs[] = {
            HWC_DISPLAY_WIDTH,
            HWC_DISPLAY_HEIGHT,
            HWC_DISPLAY_NO_ATTRIBUTE
        };
        hwc->getDisplayAttributes(hwc, 0, 0, attrs, values);
        width = values[0];
        height = values[1];

        sp<IGraphicBufferAlloc> alloc = new GraphicBufferAlloc();
        sp<BufferQueue> bq = new BufferQueue(alloc);
        fb_surface = new FramebufferSurface(0, width, height, HAL_PIXEL_FORMAT_RGBA_8888, bq);
        st_client = new Surface(static_cast<sp<IGraphicBufferProducer> >(bq));
        st_client->perform(st_client.get(), NATIVE_WINDOW_SET_BUFFER_COUNT, 2);
        st_client->perform(st_client.get(), NATIVE_WINDOW_SET_USAGE, GRALLOC_USAGE_HW_FB |
                            GRALLOC_USAGE_HW_RENDER | GRALLOC_USAGE_HW_COMPOSER);

        mList = (hwc_display_contents_1_t *) malloc(sizeof(*mList) + (sizeof(hwc_layer_1_t)*2));
        hwc->blank(hwc, HWC_DISPLAY_PRIMARY, 0);

        *egl_native_window = st_client.get();
    }

    void swap_buffers(EGLDisplay dpy, EGLSurface sur)
    {
        mList->outbuf = NULL;
        mList->outbufAcquireFenceFd = -1;

        eglSwapBuffers(dpy, sur);

        buffer_handle_t buf = fb_surface->lastHandle;
        int fence = fb_surface->GetPrevFBAcquireFd();

        hwc_display_contents_1_t *displays[HWC_NUM_DISPLAY_TYPES] = {NULL};
        const hwc_rect_t r = { 0, 0, width, height };
        displays[HWC_DISPLAY_PRIMARY] = mList;
        mList->retireFenceFd = -1;
        mList->numHwLayers = 2;
        mList->flags = HWC_GEOMETRY_CHANGED;
        mList->hwLayers[0].compositionType = HWC_FRAMEBUFFER;
        mList->hwLayers[0].hints = 0;
        /* Skip this layer so the hwc module doesn't complain about null handles */
        mList->hwLayers[0].flags = HWC_SKIP_LAYER;
        mList->hwLayers[0].backgroundColor.r = 0;
        mList->hwLayers[0].backgroundColor.g = 0;
        mList->hwLayers[0].backgroundColor.b = 0;
        mList->hwLayers[0].backgroundColor.a = 0;
        mList->hwLayers[0].acquireFenceFd = -1;
        mList->hwLayers[0].releaseFenceFd = -1;
        /* hwc module checks displayFrame even though it shouldn't */
        mList->hwLayers[0].displayFrame = r;
        mList->hwLayers[1].compositionType = HWC_FRAMEBUFFER_TARGET;
        mList->hwLayers[1].hints = 0;
        mList->hwLayers[1].flags = 0;
        mList->hwLayers[1].handle = buf;
        mList->hwLayers[1].transform = 0;
        mList->hwLayers[1].blending = HWC_BLENDING_NONE;
        if (hwc->common.version >= HWC_DEVICE_API_VERSION_1_3) {
            mList->hwLayers[1].sourceCropf.left = 0;
            mList->hwLayers[1].sourceCropf.top = 0;
            mList->hwLayers[1].sourceCropf.right = width;
            mList->hwLayers[1].sourceCropf.bottom = height;
        } else {
            mList->hwLayers[1].sourceCrop = r;
        }
        mList->hwLayers[1].displayFrame = r;
        mList->hwLayers[1].visibleRegionScreen.numRects = 1;
        mList->hwLayers[1].visibleRegionScreen.rects = &mList->hwLayers[1].displayFrame;
        mList->hwLayers[1].acquireFenceFd = fence;
        mList->hwLayers[1].releaseFenceFd = -1;
        mList->hwLayers[1].planeAlpha = 0xFF;
        hwc->prepare(hwc, HWC_NUM_DISPLAY_TYPES, displays);
        int err = hwc->set(hwc, HWC_NUM_DISPLAY_TYPES, displays);
        fb_surface->setReleaseFenceFd(mList->hwLayers[1].releaseFenceFd);
        if (mList->retireFenceFd >= 0)
            close(mList->retireFenceFd);
    }

private:
    hw_module_t const *gralloc_module;
    hw_module_t const *hwc_module;
    hwc_composer_device_1_t *hwc;
    sp<FramebufferSurface> fb_surface;
    sp<ANativeWindow> st_client;
    int width;
    int height;
    hwc_display_contents_1_t *mList;
};

extern "C"
{
    struct Display *display_create(void **egl_native_window)
    {
        return new Display(egl_native_window);
    }

    void display_swap_buffers(struct Display *d, EGLDisplay dpy, EGLSurface sur)
    {
        d->swap_buffers(dpy, sur);
    }
}
