#pragma once

#include <QuartzCore/QuartzCore.hpp>

#include "Cafe/HW/Latte/Renderer/Metal/MetalCommon.h"
#include "util/math/vector2.h"

class MetalLayerHandle
{
public:
    MetalLayerHandle() = default;
    MetalLayerHandle(MTL::Device* device, const Vector2i& size, bool mainWindow);

    MetalLayerHandle(MetalLayerHandle&& other) noexcept
        : m_layer(other.m_layer), m_layerScaleX(other.m_layerScaleX),
          m_layerScaleY(other.m_layerScaleY), m_drawable(other.m_drawable)
    {
        other.m_layer    = nullptr;
        other.m_drawable = nullptr;
    }

    MetalLayerHandle& operator=(MetalLayerHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (m_layer) m_layer->release();
            m_layer          = other.m_layer;
            m_layerScaleX    = other.m_layerScaleX;
            m_layerScaleY    = other.m_layerScaleY;
            m_drawable       = other.m_drawable;
            other.m_layer    = nullptr;
            other.m_drawable = nullptr;
        }
        return *this;
    }

    MetalLayerHandle(const MetalLayerHandle&)            = delete;
    MetalLayerHandle& operator=(const MetalLayerHandle&) = delete;

    ~MetalLayerHandle();

    void Resize(const Vector2i& size);

    bool AcquireDrawable();

    void PresentDrawable(MTL::CommandBuffer* commandBuffer);

    CA::MetalLayer* GetLayer() const { return m_layer; }

    CA::MetalDrawable* GetDrawable() const { return m_drawable; }

private:
    CA::MetalLayer* m_layer = nullptr;
    float m_layerScaleX, m_layerScaleY;

    CA::MetalDrawable* m_drawable = nullptr;
};
