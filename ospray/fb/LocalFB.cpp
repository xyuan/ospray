// ======================================================================== //
// Copyright 2009-2019 Intel Corporation                                    //
//                                                                          //
// Licensed under the Apache License, Version 2.0 (the "License");          //
// you may not use this file except in compliance with the License.         //
// You may obtain a copy of the License at                                  //
//                                                                          //
//     http://www.apache.org/licenses/LICENSE-2.0                           //
//                                                                          //
// Unless required by applicable law or agreed to in writing, software      //
// distributed under the License is distributed on an "AS IS" BASIS,        //
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. //
// See the License for the specific language governing permissions and      //
// limitations under the License.                                           //
// ======================================================================== //

// ospray
#include "LocalFB.h"
#include "LocalFB_ispc.h"
#include "ImageOp.h"
#include "FrameOp.h"

namespace ospray {

  LocalFrameBuffer::LocalFrameBuffer(const vec2i &_size,
                                     ColorBufferFormat _colorBufferFormat,
                                     const uint32 channels,
                                     void *colorBufferToUse)
      : FrameBuffer(_size, _colorBufferFormat, channels),
        tileErrorRegion(hasVarianceBuffer ? getNumTiles() : vec2i(0))
  {
    Assert(size.x > 0);
    Assert(size.y > 0);
    if (colorBufferToUse)
      colorBuffer = colorBufferToUse;
    else {
      switch (colorBufferFormat) {
      case OSP_FB_NONE:
        colorBuffer = nullptr;
        break;
      case OSP_FB_RGBA8:
      case OSP_FB_SRGBA:
        colorBuffer = (uint32 *)alignedMalloc(sizeof(uint32) * size.x * size.y);
        break;
      case OSP_FB_RGBA32F:
        colorBuffer = (vec4f *)alignedMalloc(sizeof(vec4f) * size.x * size.y);
        break;
      }
    }

    depthBuffer =
        hasDepthBuffer ? alignedMalloc<float>(size.x * size.y) : nullptr;

    accumBuffer =
        hasAccumBuffer ? alignedMalloc<vec4f>(size.x * size.y) : nullptr;

    const size_t bytes = sizeof(int32) * getTotalTiles();
    tileAccumID        = (int32 *)alignedMalloc(bytes);
    memset(tileAccumID, 0, bytes);

    varianceBuffer =
        hasVarianceBuffer ? alignedMalloc<vec4f>(size.x * size.y) : nullptr;

    normalBuffer =
        hasNormalBuffer ? alignedMalloc<vec3f>(size.x * size.y) : nullptr;

    albedoBuffer =
        hasAlbedoBuffer ? alignedMalloc<vec3f>(size.x * size.y) : nullptr;

    ispcEquivalent = ispc::LocalFrameBuffer_create(this,
                                                   size.x,
                                                   size.y,
                                                   colorBufferFormat,
                                                   colorBuffer,
                                                   depthBuffer,
                                                   accumBuffer,
                                                   varianceBuffer,
                                                   normalBuffer,
                                                   albedoBuffer,
                                                   tileAccumID);
  }

  LocalFrameBuffer::~LocalFrameBuffer()
  {
    alignedFree(depthBuffer);
    alignedFree(colorBuffer);
    alignedFree(accumBuffer);
    alignedFree(varianceBuffer);
    alignedFree(normalBuffer);
    alignedFree(albedoBuffer);
    alignedFree(tileAccumID);
  }

  std::string LocalFrameBuffer::toString() const
  {
    return "ospray::LocalFrameBuffer";
  }

  void LocalFrameBuffer::clear()
  {
    frameID = -1;  // we increment at the start of the frame
    // it is only necessary to reset the accumID,
    // LocalFrameBuffer_accumulateTile takes care of clearing the
    // accumulating buffers
    memset(tileAccumID, 0, getTotalTiles() * sizeof(int32));

    // always also clear error buffer (if present)
    if (hasVarianceBuffer) {
      tileErrorRegion.clear();
    }
  }

  void LocalFrameBuffer::setTile(Tile &tile)
  {
    if (accumBuffer) {
      const float err =
          ispc::LocalFrameBuffer_accumulateTile(getIE(), (ispc::Tile &)tile);
      if ((tile.accumID & 1) == 1)
        tileErrorRegion.update(tile.region.lower / TILE_SIZE, err);
    }
    if (hasAlbedoBuffer)
      ispc::LocalFrameBuffer_accumulateAuxTile(getIE(),
                                               (ispc::Tile &)tile,
                                               (ispc::vec3f *)albedoBuffer,
                                               tile.ar,
                                               tile.ag,
                                               tile.ab);
    if (hasNormalBuffer)
      ispc::LocalFrameBuffer_accumulateAuxTile(getIE(),
                                               (ispc::Tile &)tile,
                                               (ispc::vec3f *)normalBuffer,
                                               tile.nx,
                                               tile.ny,
                                               tile.nz);

    if (imageOpData) {
      std::for_each(imageOpData->begin<ImageOp *>(),
                    imageOpData->begin<ImageOp *>() + firstFrameOperation,
                    [&](ImageOp *iop) { 
                      #if 0
                      PixelOp *pop = dynamic_cast<PixelOp *>(iop);
                      if (pop) {
                        //p->postAccum(this, tile);
                      }
                      #endif
                      TileOp *top = dynamic_cast<TileOp *>(iop);
                      if (top) {
                        top->process(this, tile);
                      }
                      // TODO: For now, frame operations must be last
                      // in the pipeline
                    });
    }

    if (colorBuffer) {
      switch (colorBufferFormat) {
      case OSP_FB_RGBA8:
        ispc::LocalFrameBuffer_writeTile_RGBA8(getIE(), (ispc::Tile &)tile);
        break;
      case OSP_FB_SRGBA:
        ispc::LocalFrameBuffer_writeTile_SRGBA(getIE(), (ispc::Tile &)tile);
        break;
      case OSP_FB_RGBA32F:
        ispc::LocalFrameBuffer_writeTile_RGBA32F(getIE(), (ispc::Tile &)tile);
        break;
      default:
        NOTIMPLEMENTED;
      }
    }
  }

  int32 LocalFrameBuffer::accumID(const vec2i &tile)
  {
    return tileAccumID[tile.y * numTiles.x + tile.x];
  }

  float LocalFrameBuffer::tileError(const vec2i &tile)
  {
    return tileErrorRegion[tile];
  }

  void LocalFrameBuffer::beginFrame()
  {
    FrameBuffer::beginFrame();

    if (imageOpData) {
      std::for_each(imageOpData->begin<ImageOp *>(),
                    imageOpData->end<ImageOp *>(),
                    [](ImageOp *p) { p->beginFrame(); });
    }
  }

  void LocalFrameBuffer::endFrame(const float errorThreshold)
  {
    if (imageOpData) {
      if (firstFrameOperation < imageOpData->size()) {
        FrameBufferView fbv(this, colorBufferFormat, colorBuffer, depthBuffer,
                            normalBuffer, albedoBuffer);

        std::for_each(imageOpData->begin<FrameOp *>() + firstFrameOperation, 
                      imageOpData->end<FrameOp *>(),
                      [&](FrameOp *f) { f->process(fbv); });
      }

      std::for_each(imageOpData->begin<ImageOp *>(),
                    imageOpData->end<ImageOp *>(),
                    [](ImageOp *p) { p->endFrame(); });
    }

    frameVariance = tileErrorRegion.refine(errorThreshold);
  }

  const void *LocalFrameBuffer::mapBuffer(OSPFrameBufferChannel channel)
  {
    const void *buf;
    switch (channel) {
    case OSP_FB_COLOR:
      buf = colorBuffer;
      break;
    case OSP_FB_DEPTH:
      buf = depthBuffer;
      break;
    case OSP_FB_NORMAL:
      buf = normalBuffer;
      break;
    case OSP_FB_ALBEDO:
      buf = albedoBuffer;
      break;
    default:
      buf = nullptr;
      break;
    }

    if (buf)
      this->refInc();

    return buf;
  }

  void LocalFrameBuffer::unmap(const void *mappedMem)
  {
    if (mappedMem) {
      if (mappedMem != colorBuffer && mappedMem != depthBuffer &&
          mappedMem != normalBuffer && mappedMem != albedoBuffer) {
        throw std::runtime_error(
            "ERROR: unmapping a pointer not created by "
            "OSPRay!");
      }
      this->refDec();
    }
  }

}  // namespace ospray
