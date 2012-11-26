// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RESOURCE_PROVIDER_H_
#define CC_RESOURCE_PROVIDER_H_

#include "base/basictypes.h"
#include "base/hash_tables.h"
#include "base/memory/scoped_ptr.h"
#include "base/threading/thread_checker.h"
#include "cc/cc_export.h"
#include "cc/graphics_context.h"
#include "cc/texture_copier.h"
#include "cc/transferable_resource.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "ui/gfx/size.h"
#include <deque>
#include <vector>

namespace WebKit {
class WebGraphicsContext3D;
}

namespace gfx {
class Rect;
class Vector2d;
}

namespace cc {

class TextureUploader;

// This class is not thread-safe and can only be called from the thread it was
// created on (in practice, the impl thread).
class CC_EXPORT ResourceProvider {
public:
    typedef unsigned ResourceId;
    typedef std::vector<ResourceId> ResourceIdArray;
    typedef base::hash_map<ResourceId, ResourceId> ResourceIdMap;
    enum TextureUsageHint { TextureUsageAny, TextureUsageFramebuffer };
    enum ResourceType {
        GLTexture = 1,
        Bitmap,
    };

    static scoped_ptr<ResourceProvider> create(GraphicsContext*);

    virtual ~ResourceProvider();

    WebKit::WebGraphicsContext3D* graphicsContext3D();
    TextureCopier* textureCopier() const { return m_textureCopier.get(); }
    int maxTextureSize() const { return m_maxTextureSize; }
    unsigned numResources() const { return m_resources.size(); }

    // Checks whether a resource is in use by a consumer.
    bool inUseByConsumer(ResourceId);


    // Producer interface.

    void setDefaultResourceType(ResourceType type) { m_defaultResourceType = type; }
    ResourceType defaultResourceType() const { return m_defaultResourceType; }
    ResourceType resourceType(ResourceId);

    // Creates a resource of the default resource type.
    ResourceId createResource(int pool, const gfx::Size&, GLenum format, TextureUsageHint);

    // You can also explicitly create a specific resource type.
    ResourceId createGLTexture(int pool, const gfx::Size&, GLenum format, TextureUsageHint);
    ResourceId createBitmap(int pool, const gfx::Size&);
    // Wraps an external texture into a GL resource.
    ResourceId createResourceFromExternalTexture(unsigned textureId);

    void deleteResource(ResourceId);

    // Deletes all resources owned by a given pool.
    void deleteOwnedResources(int pool);

    // Update pixels from image, copying sourceRect (in image) into destRect (in the resource).
    void setPixels(ResourceId, const uint8_t* image, const gfx::Rect& imageRect, const gfx::Rect& sourceRect, const gfx::Vector2d& destOffset);

    // Check upload status.
    size_t numBlockingUploads();
    void markPendingUploadsAsNonBlocking();
    double estimatedUploadsPerSecond();
    void flushUploads();

    // Flush all context operations, kicking uploads and ensuring ordering with
    // respect to other contexts.
    void flush();

    // Only flush the command buffer if supported.
    // Returns true if the shallow flush occurred, false otherwise.
    bool shallowFlushIfSupported();

    // Creates accounting for a child, and associate it with a pool. Resources
    // transfered from that child will go to that pool. Returns a child ID.
    int createChild(int pool);

    // Destroys accounting for the child, deleting all resources from that pool.
    void destroyChild(int child);

    // Gets the child->parent resource ID map.
    const ResourceIdMap& getChildToParentMap(int child) const;

    // Prepares resources to be transfered to the parent, moving them to
    // mailboxes and serializing meta-data into TransferableResources.
    // Resources are not removed from the ResourceProvider, but are marked as
    // "in use".
    void prepareSendToParent(const ResourceIdArray&, TransferableResourceList*);

    // Prepares resources to be transfered back to the child, moving them to
    // mailboxes and serializing meta-data into TransferableResources.
    // Resources are removed from the ResourceProvider. Note: the resource IDs
    // passed are in the parent namespace and will be translated to the child
    // namespace when returned.
    void prepareSendToChild(int child, const ResourceIdArray&, TransferableResourceList*);

    // Receives resources from a child, moving them from mailboxes. Resource IDs
    // passed are in the child namespace, and will be translated to the parent
    // namespace, added to the child->parent map.
    // NOTE: if the syncPoint filed in TransferableResourceList is set, this
    // will wait on it.
    void receiveFromChild(int child, const TransferableResourceList&);

    // Receives resources from the parent, moving them from mailboxes. Resource IDs
    // passed are in the child namespace.
    // NOTE: if the syncPoint filed in TransferableResourceList is set, this
    // will wait on it.
    void receiveFromParent(const TransferableResourceList&);

    // The following lock classes are part of the ResourceProvider API and are
    // needed to read and write the resource contents. The user must ensure
    // that they only use GL locks on GL resources, etc, and this is enforced
    // by assertions.
    class CC_EXPORT ScopedReadLockGL {
    public:
        ScopedReadLockGL(ResourceProvider*, ResourceProvider::ResourceId);
        ~ScopedReadLockGL();

        unsigned textureId() const { return m_textureId; }

    private:
        ResourceProvider* m_resourceProvider;
        ResourceProvider::ResourceId m_resourceId;
        unsigned m_textureId;

        DISALLOW_COPY_AND_ASSIGN(ScopedReadLockGL);
    };

    class CC_EXPORT ScopedWriteLockGL {
    public:
        ScopedWriteLockGL(ResourceProvider*, ResourceProvider::ResourceId);
        ~ScopedWriteLockGL();

        unsigned textureId() const { return m_textureId; }

    private:
        ResourceProvider* m_resourceProvider;
        ResourceProvider::ResourceId m_resourceId;
        unsigned m_textureId;

        DISALLOW_COPY_AND_ASSIGN(ScopedWriteLockGL);
    };

    class CC_EXPORT ScopedReadLockSoftware {
    public:
        ScopedReadLockSoftware(ResourceProvider*, ResourceProvider::ResourceId);
        ~ScopedReadLockSoftware();

        const SkBitmap* skBitmap() const { return &m_skBitmap; }

    private:
        ResourceProvider* m_resourceProvider;
        ResourceProvider::ResourceId m_resourceId;
        SkBitmap m_skBitmap;

        DISALLOW_COPY_AND_ASSIGN(ScopedReadLockSoftware);
    };

    class CC_EXPORT ScopedWriteLockSoftware {
    public:
        ScopedWriteLockSoftware(ResourceProvider*, ResourceProvider::ResourceId);
        ~ScopedWriteLockSoftware();

        SkCanvas* skCanvas() { return m_skCanvas.get(); }

    private:
        ResourceProvider* m_resourceProvider;
        ResourceProvider::ResourceId m_resourceId;
        SkBitmap m_skBitmap;
        scoped_ptr<SkCanvas> m_skCanvas;

        DISALLOW_COPY_AND_ASSIGN(ScopedWriteLockSoftware);
    };

    // Acquire pixel buffer for resource. The pixel buffer can be used to
    // set resource pixels without performing unnecessary copying.
    void acquirePixelBuffer(ResourceId id);
    void releasePixelBuffer(ResourceId id);

    // Map/unmap the acquired pixel buffer.
    uint8_t* mapPixelBuffer(ResourceId id);
    void unmapPixelBuffer(ResourceId id);

    // Update pixels from acquired pixel buffer.
    void setPixelsFromBuffer(ResourceId id);

private:
    struct Resource {
        Resource();
        Resource(unsigned textureId, int pool, const gfx::Size& size, GLenum format);
        Resource(uint8_t* pixels, int pool, const gfx::Size& size, GLenum format);

        unsigned glId;
        // Pixel buffer used for set pixels without unnecessary copying.
        unsigned glPixelBufferId;
        Mailbox mailbox;
        uint8_t* pixels;
        uint8_t* pixelBuffer;
        int pool;
        int lockForReadCount;
        bool lockedForWrite;
        bool external;
        bool exported;
        bool markedForDeletion;
        gfx::Size size;
        GLenum format;
        ResourceType type;
    };
    typedef base::hash_map<ResourceId, Resource> ResourceMap;
    struct Child {
        Child();
        ~Child();

        int pool;
        ResourceIdMap childToParentMap;
        ResourceIdMap parentToChildMap;
    };
    typedef base::hash_map<int, Child> ChildMap;

    explicit ResourceProvider(GraphicsContext*);
    bool initialize();

    const Resource* lockForRead(ResourceId);
    void unlockForRead(ResourceId);
    const Resource* lockForWrite(ResourceId);
    void unlockForWrite(ResourceId);
    static void populateSkBitmapWithResource(SkBitmap*, const Resource*);

    bool transferResource(WebKit::WebGraphicsContext3D*, ResourceId, TransferableResource*);
    void deleteResourceInternal(ResourceMap::iterator it);

    GraphicsContext* m_context;
    ResourceId m_nextId;
    ResourceMap m_resources;
    int m_nextChild;
    ChildMap m_children;

    ResourceType m_defaultResourceType;
    bool m_useTextureStorageExt;
    bool m_useTextureUsageHint;
    bool m_useShallowFlush;
    scoped_ptr<TextureUploader> m_textureUploader;
    scoped_ptr<AcceleratedTextureCopier> m_textureCopier;
    int m_maxTextureSize;

    base::ThreadChecker m_threadChecker;

    DISALLOW_COPY_AND_ASSIGN(ResourceProvider);
};

}

#endif  // CC_RESOURCE_PROVIDER_H_
