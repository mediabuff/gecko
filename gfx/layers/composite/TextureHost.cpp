/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/TextureHost.h"
#include "mozilla/layers/LayersSurfaces.h"
#include "LayersLogging.h"
#include "nsPrintfCString.h"
#include "mozilla/ipc/Shmem.h"
#include "ipc/AutoOpenSurface.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/YCbCrImageDataSerializer.h"
#include "gfx2DGlue.h"
#include "mozilla/gfx/2D.h"


namespace mozilla {
namespace layers {

// implemented in TextureOGL.cpp
TemporaryRef<DeprecatedTextureHost> CreateDeprecatedTextureHostOGL(SurfaceDescriptorType aDescriptorType,
                                                           uint32_t aDeprecatedTextureHostFlags,
                                                           uint32_t aTextureFlags);
// implemented in BasicCompositor.cpp
TemporaryRef<DeprecatedTextureHost> CreateBasicDeprecatedTextureHost(SurfaceDescriptorType aDescriptorType,
                                                             uint32_t aDeprecatedTextureHostFlags,
                                                             uint32_t aTextureFlags);

TemporaryRef<DeprecatedTextureHost> CreateDeprecatedTextureHostD3D9(SurfaceDescriptorType aDescriptorType,
                                                            uint32_t aDeprecatedTextureHostFlags,
                                                            uint32_t aTextureFlags)
{
  NS_RUNTIMEABORT("not implemented");
  return nullptr;
}

#ifdef XP_WIN
TemporaryRef<DeprecatedTextureHost> CreateDeprecatedTextureHostD3D11(SurfaceDescriptorType aDescriptorType,
                                                             uint32_t aDeprecatedTextureHostFlags,
                                                             uint32_t aTextureFlags);
#endif

/* static */ TemporaryRef<DeprecatedTextureHost>
DeprecatedTextureHost::CreateDeprecatedTextureHost(SurfaceDescriptorType aDescriptorType,
                                           uint32_t aDeprecatedTextureHostFlags,
                                           uint32_t aTextureFlags)
{
  switch (Compositor::GetBackend()) {
    case LAYERS_OPENGL:
      return CreateDeprecatedTextureHostOGL(aDescriptorType,
                                        aDeprecatedTextureHostFlags,
                                        aTextureFlags);
    case LAYERS_D3D9:
      return CreateDeprecatedTextureHostD3D9(aDescriptorType,
                                         aDeprecatedTextureHostFlags,
                                         aTextureFlags);
#ifdef XP_WIN
    case LAYERS_D3D11:
      return CreateDeprecatedTextureHostD3D11(aDescriptorType,
                                          aDeprecatedTextureHostFlags,
                                          aTextureFlags);
#endif
    case LAYERS_BASIC:
      return CreateBasicDeprecatedTextureHost(aDescriptorType,
                                          aDeprecatedTextureHostFlags,
                                          aTextureFlags);
    default:
      MOZ_CRASH("Couldn't create texture host");
  }
}

// implemented in TextureOGL.cpp
TemporaryRef<TextureHost> CreateTextureHostOGL(uint64_t aID,
                                               const SurfaceDescriptor& aDesc,
                                               ISurfaceAllocator* aDeallocator,
                                               TextureFlags aFlags);

// static
TemporaryRef<TextureHost>
TextureHost::Create(uint64_t aID,
                    const SurfaceDescriptor& aDesc,
                    ISurfaceAllocator* aDeallocator,
                    TextureFlags aFlags)
{
  switch (Compositor::GetBackend()) {
    case LAYERS_OPENGL:
      return CreateTextureHostOGL(aID, aDesc, aDeallocator, aFlags);
    case LAYERS_BASIC:
      return CreateBackendIndependentTextureHost(aID,
                                                 aDesc,
                                                 aDeallocator,
                                                 aFlags);
#ifdef XP_WIN
    case LAYERS_D3D11:
    case LAYERS_D3D9:
      // XXX - not implemented yet
#endif
    default:
      MOZ_CRASH("Couldn't create texture host");
      return nullptr;
  }
}

TemporaryRef<TextureHost>
CreateBackendIndependentTextureHost(uint64_t aID,
                                    const SurfaceDescriptor& aDesc,
                                    ISurfaceAllocator* aDeallocator,
                                    TextureFlags aFlags)
{
  RefPtr<TextureHost> result;
  switch (aDesc.type()) {
    case SurfaceDescriptor::TSurfaceDescriptorShmem: {
      const SurfaceDescriptorShmem& descriptor = aDesc.get_SurfaceDescriptorShmem();
      result = new ShmemTextureHost(aID,
                                    descriptor.data(),
                                    descriptor.format(),
                                    aDeallocator,
                                    aFlags);
      break;
    }
    case SurfaceDescriptor::TSurfaceDescriptorMemory: {
      const SurfaceDescriptorMemory& descriptor = aDesc.get_SurfaceDescriptorMemory();
      result = new MemoryTextureHost(aID,
                                     reinterpret_cast<uint8_t*>(descriptor.data()),
                                     descriptor.format(),
                                     aFlags);
      break;
    }
    default: {
      NS_WARNING("No backend independent TextureHost for this descriptor type");
    }
  }
  return result;
}

DeprecatedTextureHost::DeprecatedTextureHost()
  : mFlags(0)
  , mBuffer(nullptr)
  , mDeAllocator(nullptr)
  , mFormat(gfx::FORMAT_UNKNOWN)
{
  MOZ_COUNT_CTOR(DeprecatedTextureHost);
}

DeprecatedTextureHost::~DeprecatedTextureHost()
{
  if (mBuffer) {
    if (!(mFlags & TEXTURE_DEALLOCATE_CLIENT)) {
      if (mDeAllocator) {
        mDeAllocator->DestroySharedSurface(mBuffer);
      } else {
        MOZ_ASSERT(mBuffer->type() == SurfaceDescriptor::Tnull_t);
      }
    }
    delete mBuffer;
  }
  MOZ_COUNT_DTOR(DeprecatedTextureHost);
}

void
DeprecatedTextureHost::Update(const SurfaceDescriptor& aImage,
                          nsIntRegion* aRegion,
                          nsIntPoint* aOffset)
{
  UpdateImpl(aImage, aRegion, aOffset);
}

void
DeprecatedTextureHost::SwapTextures(const SurfaceDescriptor& aImage,
                                SurfaceDescriptor* aResult,
                                nsIntRegion* aRegion)
{
  SwapTexturesImpl(aImage, aRegion);

  MOZ_ASSERT(mBuffer, "trying to swap a non-buffered texture host?");
  if (aResult) {
    *aResult = *mBuffer;
  }
  *mBuffer = aImage;
}

#ifdef MOZ_LAYERS_HAVE_LOG
void
TextureSource::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("UnknownTextureSource (0x%p)", this);
}

void
DeprecatedTextureHost::PrintInfo(nsACString& aTo, const char* aPrefix)
{
  aTo += aPrefix;
  aTo += nsPrintfCString("%s (0x%p)", Name(), this);
  AppendToString(aTo, GetSize(), " [size=", "]");
  AppendToString(aTo, GetFormat(), " [format=", "]");
  AppendToString(aTo, mFlags, " [flags=", "]");
}
#endif // MOZ_LAYERS_HAVE_LOG







BufferTextureHost::BufferTextureHost(uint64_t aID,
                                     gfx::SurfaceFormat aFormat,
                                     TextureFlags aFlags)
: TextureHost(aID, aFlags)
, mCompositor(nullptr)
, mFormat(aFormat)
, mUpdateSerial(1)
, mLocked(false)
, mPartialUpdate(false)
{}

BufferTextureHost::~BufferTextureHost()
{}

void
BufferTextureHost::Updated(const nsIntRegion* aRegion)
{
  ++mUpdateSerial;
  if (aRegion) {
    mPartialUpdate = true;
    mMaybeUpdatedRegion = *aRegion;
  } else {
    mPartialUpdate = false;
  }
}

void
BufferTextureHost::SetCompositor(Compositor* aCompositor)
{
  if (mCompositor == aCompositor) {
    return;
  }
  DeallocateDeviceData();
  mCompositor = aCompositor;
}

void
BufferTextureHost::DeallocateDeviceData()
{
  RefPtr<NewTextureSource> it = mFirstSource;
  while (it) {
    it->DeallocateDeviceData();
    it = it->GetNextSibling();
  }
}

bool
BufferTextureHost::Lock()
{
  mLocked = true;
  return true;
}

void
BufferTextureHost::Unlock()
{
  mLocked = false;
}

NewTextureSource*
BufferTextureHost::GetTextureSources()
{
  MOZ_ASSERT(mLocked, "should never be called while not locked");
  if (!mFirstSource || mUpdateSerial != mFirstSource->GetUpdateSerial()) {
    if (!Upload(mPartialUpdate ? &mMaybeUpdatedRegion : nullptr)) {
      return nullptr;
    }
    mFirstSource->SetUpdateSerial(mUpdateSerial);
  }
  return mFirstSource;
}

gfx::SurfaceFormat
BufferTextureHost::GetFormat() const
{
  // mFormat is the format of the data that we share with the content process.
  // GetFormat, on the other hand, expects the format that we present to the
  // Compositor (it is used to choose the effect type).
  // if the compositor does not support YCbCr effects, we give it a RGBX texture
  // instead (see BufferTextureHost::Upload)
  if (mFormat == gfx::FORMAT_YUV &&
    mCompositor &&
    !mCompositor->SupportsEffect(EFFECT_YCBCR)) {
    return gfx::FORMAT_R8G8B8X8;
  }
  return mFormat;
}

bool
BufferTextureHost::Upload(nsIntRegion *aRegion)
{
  if (mFormat == gfx::FORMAT_UNKNOWN) {
    NS_WARNING("BufferTextureHost: unsupported format!");
    return false;
  } else if (mFormat == gfx::FORMAT_YUV) {
    YCbCrImageDataDeserializer yuvDeserializer(GetBuffer());
    MOZ_ASSERT(yuvDeserializer.IsValid());

    if (!mCompositor->SupportsEffect(EFFECT_YCBCR)) {
      RefPtr<gfx::DataSourceSurface> surf = yuvDeserializer.ToDataSourceSurface();
      if (!mFirstSource) {
        mFirstSource = mCompositor->CreateDataTextureSource(mFlags);
      }
      mFirstSource->Update(surf, mFlags, aRegion);
      return true;
    }

    RefPtr<DataTextureSource> srcY;
    RefPtr<DataTextureSource> srcU;
    RefPtr<DataTextureSource> srcV;
    if (!mFirstSource) {
      srcY = mCompositor->CreateDataTextureSource(mFlags);
      srcU = mCompositor->CreateDataTextureSource(mFlags);
      srcV = mCompositor->CreateDataTextureSource(mFlags);
      mFirstSource = srcY;
      srcY->SetNextSibling(srcU);
      srcU->SetNextSibling(srcV);
    } else {
      // mFormat never changes so if this was created as a YCbCr host and already
      // contains a source it should already have 3 sources.
      // BufferTextureHost only uses DataTextureSources so it is safe to assume
      // all 3 sources are DataTextureSource.
      MOZ_ASSERT(mFirstSource->GetNextSibling());
      MOZ_ASSERT(mFirstSource->GetNextSibling()->GetNextSibling());
      srcY = mFirstSource;
      srcU = mFirstSource->GetNextSibling()->AsDataTextureSource();
      srcV = mFirstSource->GetNextSibling()->GetNextSibling()->AsDataTextureSource();
    }


    RefPtr<gfx::DataSourceSurface> tempY =
      gfx::Factory::CreateWrappingDataSourceSurface(yuvDeserializer.GetYData(),
                                                    yuvDeserializer.GetYStride(),
                                                    gfx::ToIntSize(yuvDeserializer.GetYSize()),
                                                    gfx::FORMAT_A8);
    RefPtr<gfx::DataSourceSurface> tempCb =
      gfx::Factory::CreateWrappingDataSourceSurface(yuvDeserializer.GetCbData(),
                                                    yuvDeserializer.GetCbCrStride(),
                                                    gfx::ToIntSize(yuvDeserializer.GetCbCrSize()),
                                                    gfx::FORMAT_A8);
    RefPtr<gfx::DataSourceSurface> tempCr =
      gfx::Factory::CreateWrappingDataSourceSurface(yuvDeserializer.GetCrData(),
                                                    yuvDeserializer.GetCbCrStride(),
                                                    gfx::ToIntSize(yuvDeserializer.GetCbCrSize()),
                                                    gfx::FORMAT_A8);
    // We don't support partial updates for Y U V textures
    NS_ASSERTION(!aRegion, "Unsupported partial updates for YCbCr textures");
    if (!srcY->Update(tempY, mFlags) ||
        !srcU->Update(tempCb, mFlags) ||
        !srcV->Update(tempCr, mFlags)) {
      NS_WARNING("failed to update the DataTextureSource");
      return false;
    }
  } else {
    // non-YCbCr case
    if (!mFirstSource) {
      mFirstSource = mCompositor->CreateDataTextureSource();
    }
    ImageDataDeserializer deserializer(GetBuffer());
    if (!deserializer.IsValid()) {
      NS_WARNING("failed to open shmem surface");
      return false;
    }

    RefPtr<gfx::DataSourceSurface> surf = deserializer.GetAsSurface();

    if (!mFirstSource->Update(surf.get(), mFlags, aRegion)) {
      NS_WARNING("failed to update the DataTextureSource");
      return false;
    }
  }
  return true;
}

already_AddRefed<gfxImageSurface>
BufferTextureHost::GetAsSurface()
{
  nsRefPtr<gfxImageSurface> result; 
  if (mFormat == gfx::FORMAT_UNKNOWN) {
    NS_WARNING("BufferTextureHost: unsupported format!");
    return nullptr;
  } else if (mFormat == gfx::FORMAT_YUV) {
    YCbCrImageDataDeserializer yuvDeserializer(GetBuffer());
    if (!yuvDeserializer.IsValid()) {
      return nullptr;
    }
    result = new gfxImageSurface(yuvDeserializer.GetYData(),
                                 yuvDeserializer.GetYSize(),
                                 yuvDeserializer.GetYStride(),
                                 gfxASurface::ImageFormatA8);
  } else {
    ImageDataDeserializer deserializer(GetBuffer());
    if (!deserializer.IsValid()) {
      return nullptr;
    }
    RefPtr<gfxImageSurface> surf = deserializer.GetAsThebesSurface();
    result = surf.get();
  }
  return result.forget();
}

ShmemTextureHost::ShmemTextureHost(uint64_t aID,
                                   const ipc::Shmem& aShmem,
                                   gfx::SurfaceFormat aFormat,
                                   ISurfaceAllocator* aDeallocator,
                                   TextureFlags aFlags)
: BufferTextureHost(aID, aFormat, aFlags)
, mShmem(new ipc::Shmem(aShmem))
, mDeallocator(aDeallocator)
{
  MOZ_COUNT_CTOR(ShmemTextureHost);
}

ShmemTextureHost::~ShmemTextureHost()
{
  DeallocateDeviceData();
  delete mShmem;
  MOZ_COUNT_DTOR(ShmemTextureHost);
}

void
ShmemTextureHost::DeallocateSharedData()
{
  if (mShmem) {
    MOZ_ASSERT(mDeallocator,
               "Shared memory would leak without a ISurfaceAllocator");
    mDeallocator->DeallocShmem(*mShmem);
  }
}

uint8_t* ShmemTextureHost::GetBuffer()
{
  return mShmem ? mShmem->get<uint8_t>() : nullptr;
}

MemoryTextureHost::MemoryTextureHost(uint64_t aID,
                                     uint8_t* aBuffer,
                                     gfx::SurfaceFormat aFormat,
                                     TextureFlags aFlags)
: BufferTextureHost(aID, aFormat, aFlags)
, mBuffer(aBuffer)
{
  MOZ_COUNT_CTOR(MemoryTextureHost);
}

MemoryTextureHost::~MemoryTextureHost()
{
  DeallocateDeviceData();
  MOZ_COUNT_DTOR(ShmemTextureHost);
}

void
MemoryTextureHost::DeallocateSharedData()
{
  delete[] mBuffer;
}

uint8_t* MemoryTextureHost::GetBuffer()
{
  return mBuffer;
}

} // namespace
} // namespace
