#pragma once

#include "SceneViewExtension.h"
#include "RenderGraphUtils.h"

struct FCapturedProbes
{
    FRDGTextureRef Albedo = nullptr;
    FRDGTextureRef Normal = nullptr;
    FRDGTextureRef Radial = nullptr;
    FRDGTextureRef Eid = nullptr;
};

class FTexelSplatViewExtension : public FSceneViewExtensionBase
{
public:
    FTexelSplatViewExtension(const FAutoRegister& AutoRegister);

    virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {}
    virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override;
    virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override;

    virtual void PostRenderBasePassDeferred_RenderThread(
        FRDGBuilder& GraphBuilder,
        FSceneView& InView,
        const FRenderTargetBindingSlots& RenderTargets,
        TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;

    virtual void PrePostProcessPass_RenderThread(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessingInputs& Inputs) override;

private:

    FVector3f CurrentGridOrigin = FVector3f::ZeroVector;
    FVector3f PrevGridOrigin = FVector3f::ZeroVector;
    float CrossfadeTimer = 1.0f;

    TMap<const FSceneView*, FCapturedProbes> CapturedProbesMap;

    // thread-safe material proxy cached on the game thread
    FMaterialRenderProxy* FallbackMaterialProxy = nullptr;
};