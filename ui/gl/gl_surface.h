// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_GL_GL_SURFACE_H_
#define UI_GL_GL_SURFACE_H_

#include <string>

#include "base/callback.h"
#include "base/memory/ref_counted.h"
#include "build/build_config.h"
#include "ui/gfx/native_widget_types.h"
#include "ui/gfx/size.h"
#include "ui/gl/gl_export.h"

namespace base {
class TimeDelta;
class TimeTicks;
}

namespace gfx {

class GLContext;

#if defined(OS_ANDROID)
class AndroidNativeWindow;
#endif

// Encapsulates a surface that can be rendered to with GL, hiding platform
// specific management.
class GL_EXPORT GLSurface : public base::RefCounted<GLSurface> {
 public:
  GLSurface();

  // (Re)create the surface. TODO(apatrick): This is an ugly hack to allow the
  // EGL surface associated to be recreated without destroying the associated
  // context. The implementation of this function for other GLSurface derived
  // classes is in a pending changelist.
  virtual bool Initialize();

  // Destroys the surface.
  virtual void Destroy() = 0;

  virtual bool Resize(const gfx::Size& size);

  // Unschedule the GpuScheduler and return true to abort the processing of
  // a GL draw call to this surface and defer it until the GpuScheduler is
  // rescheduled.
  virtual bool DeferDraws();

  // Unschedule the GpuScheduler and return true to abort the processing of
  // a GL SwapBuffers call to this surface and defer it until the GpuScheduler
  // is rescheduled.
  virtual bool DeferSwapBuffers();

  // Returns true if this surface is offscreen.
  virtual bool IsOffscreen() = 0;

  // Swaps front and back buffers. This has no effect for off-screen
  // contexts.
  virtual bool SwapBuffers() = 0;

  // Get the size of the surface.
  virtual gfx::Size GetSize() = 0;

#if defined(OS_ANDROID)
  virtual void SetNativeWindow(AndroidNativeWindow* window) { }
#endif

  // Get the underlying platform specific surface "handle".
  virtual void* GetHandle() = 0;

  // Returns space separated list of surface specific extensions.
  // The surface must be current.
  virtual std::string GetExtensions();

  bool HasExtension(const char* name);

  // Returns the internal frame buffer object name if the surface is backed by
  // FBO. Otherwise returns 0.
  virtual unsigned int GetBackingFrameBufferObject();

  // Copy part of the backbuffer to the frontbuffer.
  virtual bool PostSubBuffer(int x, int y, int width, int height);

  static bool InitializeOneOff();

  // Called after a context is made current with this surface. Returns false
  // on error.
  virtual bool OnMakeCurrent(GLContext* context);

  // Used for explicit buffer management.
  virtual void SetBackbufferAllocation(bool allocated);
  virtual void SetFrontbufferAllocation(bool allocated);

  // Get a handle used to share the surface with another process. Returns null
  // if this is not possible.
  virtual void* GetShareHandle();

  // Get the platform specific display on which this surface resides, if
  // available.
  virtual void* GetDisplay();

  // Get the platfrom specific configuration for this surface, if available.
  virtual void* GetConfig();

  // Get the GL pixel format of the surface, if available.
  virtual unsigned GetFormat();

  typedef base::Callback<void(const base::TimeTicks timebase,
                              const base::TimeDelta interval)>
      UpdateVSyncCallback;

  // Get the time of the most recent screen refresh, along with the time
  // between consecutive refreshes. The callback is called as soon as
  // the data is available: it could be immediately from this method,
  // later via a PostTask to the current MessageLoop, or never (if we have
  // no data source). We provide the strong guarantee that the callback will
  // not be called once the instance of this class is destroyed.
  virtual void GetVSyncParameters(const UpdateVSyncCallback& callback);

  // Create a GL surface that renders directly to a view.
  static scoped_refptr<GLSurface> CreateViewGLSurface(
      bool software,
      gfx::AcceleratedWidget window);

  // Create a GL surface used for offscreen rendering.
  static scoped_refptr<GLSurface> CreateOffscreenGLSurface(
      bool software,
      const gfx::Size& size);

  static GLSurface* GetCurrent();

 protected:
  virtual ~GLSurface();
  static bool InitializeOneOffInternal();
  static void SetCurrent(GLSurface* surface);

  static bool ExtensionsContain(const char* extensions, const char* name);

 private:
  friend class base::RefCounted<GLSurface>;
  friend class GLContext;

  DISALLOW_COPY_AND_ASSIGN(GLSurface);
};

// Implementation of GLSurface that forwards all calls through to another
// GLSurface.
class GL_EXPORT GLSurfaceAdapter : public GLSurface {
 public:
  explicit GLSurfaceAdapter(GLSurface* surface);

  virtual bool Initialize() OVERRIDE;
  virtual void Destroy() OVERRIDE;
  virtual bool Resize(const gfx::Size& size) OVERRIDE;
  virtual bool DeferDraws() OVERRIDE;
  virtual bool DeferSwapBuffers() OVERRIDE;
  virtual bool IsOffscreen() OVERRIDE;
  virtual bool SwapBuffers() OVERRIDE;
  virtual bool PostSubBuffer(int x, int y, int width, int height) OVERRIDE;
  virtual std::string GetExtensions() OVERRIDE;
  virtual gfx::Size GetSize() OVERRIDE;
  virtual void* GetHandle() OVERRIDE;
  virtual unsigned int GetBackingFrameBufferObject() OVERRIDE;
  virtual bool OnMakeCurrent(GLContext* context) OVERRIDE;
  virtual void SetBackbufferAllocation(bool allocated) OVERRIDE;
  virtual void SetFrontbufferAllocation(bool allocated) OVERRIDE;
  virtual void* GetShareHandle() OVERRIDE;
  virtual void* GetDisplay() OVERRIDE;
  virtual void* GetConfig() OVERRIDE;
  virtual unsigned GetFormat() OVERRIDE;
  virtual void GetVSyncParameters(const UpdateVSyncCallback& callback) OVERRIDE;

  GLSurface* surface() const { return surface_.get(); }

 protected:
  virtual ~GLSurfaceAdapter();

 private:
  scoped_refptr<GLSurface> surface_;

  DISALLOW_COPY_AND_ASSIGN(GLSurfaceAdapter);
};

}  // namespace gfx

#endif  // UI_GL_GL_SURFACE_H_
