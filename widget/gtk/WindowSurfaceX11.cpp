/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WindowSurfaceX11.h"
#include "gfxPlatform.h"

namespace mozilla {
namespace widget {

WindowSurfaceX11::WindowSurfaceX11(Display* aDisplay,
                                   Window aWindow,
                                   Visual* aVisual,
                                   unsigned int aDepth)
  : mDisplay(aDisplay)
  , mWindow(aWindow)
  , mVisual(aVisual)
  , mDepth(aDepth)
  , mFormat(GetVisualFormat(aVisual, aDepth))
  , mGC(None)
{
  MOZ_ASSERT(mFormat != gfx::SurfaceFormat::UNKNOWN,
             "Could not find SurfaceFormat for visual!");
}

WindowSurfaceX11::~WindowSurfaceX11()
{
  if (mGC != None)
    XFreeGC(mDisplay, mGC);
}

void
WindowSurfaceX11::Commit(const LayoutDeviceIntRegion& aInvalidRegion)
{
  AutoTArray<XRectangle, 32> xrects;
  xrects.SetCapacity(aInvalidRegion.GetNumRects());

  for (auto iter = aInvalidRegion.RectIter(); !iter.Done(); iter.Next()) {
    const mozilla::LayoutDeviceIntRect &r = iter.Get();
    XRectangle xrect = { (short)r.x, (short)r.y, (unsigned short)r.width, (unsigned short)r.height };
    xrects.AppendElement(xrect);
  }

  if (!mGC) {
    mGC = XCreateGC(mDisplay, mWindow, 0, nullptr);
    if (!mGC) {
      NS_WARNING("Couldn't create X11 graphics context for window!");
      return;
    }
  }

  XSetClipRectangles(mDisplay, mGC, 0, 0, xrects.Elements(), xrects.Length(), YXBanded);
  CommitToDrawable(mWindow, mGC, aInvalidRegion);
}

/* static */
gfx::SurfaceFormat
WindowSurfaceX11::GetVisualFormat(const Visual* aVisual, unsigned int aDepth)
{
  switch (aDepth) {
  case 32:
    if (aVisual->red_mask == 0xff0000 &&
        aVisual->green_mask == 0xff00 &&
        aVisual->blue_mask == 0xff) {
      return gfx::SurfaceFormat::B8G8R8A8;
    }
    break;
  case 24:
    // Only support the BGRX layout, and report it as BGRA to the compositor.
    // The alpha channel will be discarded when we put the image.
    // Cairo/pixman lacks some fast paths for compositing BGRX onto BGRA, so
    // just report it as BGRX directly in that case.
    if (aVisual->red_mask == 0xff0000 &&
        aVisual->green_mask == 0xff00 &&
        aVisual->blue_mask == 0xff) {
      gfx::BackendType backend = gfxPlatform::GetPlatform()->GetDefaultContentBackend();
      return backend == gfx::BackendType::CAIRO ? gfx::SurfaceFormat::B8G8R8X8
                                                : gfx::SurfaceFormat::B8G8R8A8;
    }
    break;
  case 16:
    if (aVisual->red_mask == 0xf800 &&
        aVisual->green_mask == 0x07e0 &&
        aVisual->blue_mask == 0x1f) {
      return gfx::SurfaceFormat::R5G6B5_UINT16;
    }
    break;
  }

  return gfx::SurfaceFormat::UNKNOWN;
}

}  // namespace widget
}  // namespace mozilla
