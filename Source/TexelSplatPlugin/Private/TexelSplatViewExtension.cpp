#include "TexelSplatViewExtension.h"
#include "PostProcess/PostProcessing.h"
#include "SceneTextures.h"
#include "TexelSplatShaders.h"
#include "RenderGraphUtils.h"
#include "ScenePrivate.h"
#include "SceneRendering.h"
#include "MeshPassProcessor.inl"
#include "InstanceCulling/InstanceCullingContext.h"
#include "SceneUniformBuffer.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"

static TAutoConsoleVariable<int32> CVarTexelSplatEnabled(
    TEXT("r.TexelSplat.Enable"),
    1,
    TEXT("Enable/Disable TexelSplat rendering. 0=off, 1=on."),
    ECVF_RenderThreadSafe
);

template <typename TView>
auto GetSceneUB(TView* View, FRDGBuilder& GraphBuilder, int) -> decltype(View->GetSceneUniforms().GetBuffer(GraphBuilder)) { return View->GetSceneUniforms().GetBuffer(GraphBuilder); }
template <typename TView>
auto GetSceneUB(TView* View, FRDGBuilder& GraphBuilder, long) -> decltype(View->SceneUniformBuffer) { return View->SceneUniformBuffer; }
template <typename TView>
auto GetSceneUB(TView* View, FRDGBuilder& GraphBuilder, float) -> decltype(((FScene*)View->Family->Scene)->UniformBuffers.ViewUniformBuffer) { return ((FScene*)View->Family->Scene)->UniformBuffers.ViewUniformBuffer; }
template <typename TView>
auto GetSceneUB(TView* View, FRDGBuilder& GraphBuilder, double) -> decltype(((FScene*)View->Family->Scene)->UniformBuffers.UpdateViewUniformBuffer(*View)) { return ((FScene*)View->Family->Scene)->UniformBuffers.UpdateViewUniformBuffer(*View); }
template <typename TView>
TRDGUniformBufferRef<FSceneUniformParameters> GetSceneUB(TView* View, FRDGBuilder& GraphBuilder) { return GetSceneUB(View, GraphBuilder, 0); }

struct FTexelSplatCapturePassShaders
{
    TShaderRef<FTexelSplatCaptureVS> VertexShader;
    TShaderRef<FTexelSplatCapturePS> PixelShader;
    TShaderRef<FMeshMaterialShader> GeometryShader;

    FMeshProcessorShaders GetUntypedShaders() const
    {
        FMeshProcessorShaders Result;
        Result.VertexShader = VertexShader;
        Result.PixelShader = PixelShader;
        Result.GeometryShader = GeometryShader;
        return Result;
    }
};

class FTexelSplatCaptureMeshProcessor : public FMeshPassProcessor
{
public:
    FTexelSplatCaptureMeshProcessor(const FScene* InScene, const FSceneView* InViewIfDynamic, FDynamicPassMeshDrawListContext* InDrawListContext, FMaterialRenderProxy* InFallbackProxy)
        : FMeshPassProcessor(EMeshPass::Num, InScene, InScene->GetFeatureLevel(), InViewIfDynamic, InDrawListContext)
        , FallbackProxy(InFallbackProxy)
    {
        PassDrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI());
        PassDrawRenderState.SetBlendState(TStaticBlendState<>::GetRHI());
    }

    virtual void AddMeshBatch(const FMeshBatch& RESTRICT MeshBatch, uint64 BatchElementMask, const FPrimitiveSceneProxy* RESTRICT PrimitiveSceneProxy, int32 StaticMeshId = -1) override
    {
        if (MeshBatch.VertexFactory->GetType() != &FLocalVertexFactory::StaticType)
            return;

        const FMaterialRenderProxy* MaterialProxy = MeshBatch.MaterialRenderProxy;
        if (!MaterialProxy) return;

        const FMaterialRenderProxy* FallbackProxyLocal = nullptr;
        const FMaterial& Material = MaterialProxy->GetMaterialWithFallback(FeatureLevel, FallbackProxyLocal);
        if (FallbackProxyLocal)
        {
            static TSet<FString> LoggedMaterials;
            FString MatName = MaterialProxy->GetFriendlyName();
            if (!LoggedMaterials.Contains(MatName))
            {
                LoggedMaterials.Add(MatName);
                const FMaterial* NoFallbackMat = MaterialProxy->GetMaterialNoFallback(FeatureLevel);
                if (NoFallbackMat)
                {
                    FMaterialShaderMap* Map = NoFallbackMat->GetRenderingThreadShaderMap();
                    if (Map)
                        Map->IsComplete(NoFallbackMat, false);
                }
            }
            return;
        }
        const FMaterialRenderProxy& EffectiveProxy = *MaterialProxy;

        if (Material.GetMaterialDomain() != MD_Surface || Material.GetBlendMode() != BLEND_Opaque)
            return;

        FVertexFactoryType* VFType = MeshBatch.VertexFactory->GetType();

        TShaderRef<FTexelSplatCaptureVS> VertexShader = Material.GetShader<FTexelSplatCaptureVS>(VFType, 0, false);
        TShaderRef<FTexelSplatCapturePS> PixelShader = Material.GetShader<FTexelSplatCapturePS>(VFType, 0, false);

        const FMaterial* FinalMaterial = &Material;
        const FMaterialRenderProxy* FinalProxy = &EffectiveProxy;

        if (!VertexShader.IsValid() || !PixelShader.IsValid())
            return;

        FTexelSplatCapturePassShaders PassShaders;
        PassShaders.VertexShader = VertexShader;
        PassShaders.PixelShader = PixelShader;

        FMeshMaterialShaderElementData ShaderElementData;
        ShaderElementData.InitializeMeshMaterialData(ViewIfDynamicMeshCommand, PrimitiveSceneProxy, MeshBatch, StaticMeshId, false);

        const FMeshDrawCommandSortKey SortKey = FMeshDrawCommandSortKey::Default;

        BuildMeshDrawCommands(
            MeshBatch, BatchElementMask, PrimitiveSceneProxy,
            *FinalProxy, *FinalMaterial, PassDrawRenderState, PassShaders,
            FM_Solid, CM_None, SortKey, EMeshPassFeatures::Default, ShaderElementData
        );
    }

private:
    FMeshPassProcessorRenderState PassDrawRenderState;
    FMaterialRenderProxy* FallbackProxy;
};

BEGIN_SHADER_PARAMETER_STRUCT(FTexelSplatCapturePassParameters, )
    SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FSceneUniformParameters, Scene)
    SHADER_PARAMETER_RDG_UNIFORM_BUFFER(FInstanceCullingGlobalUniforms, InstanceCulling)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

FTexelSplatViewExtension::FTexelSplatViewExtension(const FAutoRegister& AutoRegister)
    : FSceneViewExtensionBase(AutoRegister)
{
}

void FTexelSplatViewExtension::SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView)
{
    if (!FallbackMaterialProxy)
    {
        UMaterial* DefaultMat = UMaterial::GetDefaultMaterial(MD_Surface);
        if (DefaultMat) FallbackMaterialProxy = DefaultMat->GetRenderProxy();
    }
}

void FTexelSplatViewExtension::BeginRenderViewFamily(FSceneViewFamily& InViewFamily)
{
    CapturedProbesMap.Empty();
}

void FTexelSplatViewExtension::PostRenderBasePassDeferred_RenderThread(
    FRDGBuilder& GraphBuilder,
    FSceneView& InView,
    const FRenderTargetBindingSlots& RenderTargets,
    TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
    if (CVarTexelSplatEnabled.GetValueOnRenderThread() == 0)
    {
        return;
    }

    if (!InView.bIsViewInfo || !InView.Family || !InView.Family->Scene) return;
    
    UWorld* World = InView.Family->Scene->GetWorld();
    if (!World || World->WorldType == EWorldType::EditorPreview || World->WorldType == EWorldType::Inactive)
        return;

    FViewInfo* MainViewInfo = (FViewInfo*)&InView;
    if (!MainViewInfo->CachedViewUniformShaderParameters) return;

    const int32 CubemapRes = 384;
    const int32 ArraySlices = 18;

    float DeltaTime = (float)InView.Family->Time.GetDeltaWorldTimeSeconds();
    FVector CameraPos = InView.ViewMatrices.GetViewOrigin();

    float GridStep = 100.0f;
    FVector NewGridOrigin = FVector(
        FMath::RoundToFloat(CameraPos.X / GridStep) * GridStep,
        FMath::RoundToFloat(CameraPos.Y / GridStep) * GridStep,
        FMath::RoundToFloat(CameraPos.Z / GridStep) * GridStep
    );

    if (CurrentGridOrigin.IsZero() && PrevGridOrigin.IsZero())
    {
        CurrentGridOrigin = (FVector3f)NewGridOrigin;
        PrevGridOrigin = (FVector3f)NewGridOrigin;
        CrossfadeTimer = 0.5f;
    }
    else if (!NewGridOrigin.Equals((FVector)CurrentGridOrigin))
    {
        PrevGridOrigin = CurrentGridOrigin;
        CurrentGridOrigin = (FVector3f)NewGridOrigin;
        CrossfadeTimer = 0.0f;
    }

    CrossfadeTimer = FMath::Min(CrossfadeTimer + DeltaTime, 0.5f);

    FVector ProbeOrigins[3] = { CameraPos, (FVector)CurrentGridOrigin, (FVector)PrevGridOrigin };

    FRotator FaceRotations[6] = {
        FRotator(0, 0, 0),        // +X
        FRotator(0, 180, 0),      // -X
        FRotator(0, 90, 0),       // +Y
        FRotator(0, -90, 0),      // -Y
        FRotator(90, 0, -90),     // +Z (Roll -90 aligns with shader)
        FRotator(-90, 0, 90)      // -Z (Roll +90 aligns with shader)
    };

    FMatrix ViewAxisSwap(
        FPlane(0, 0, 1, 0),
        FPlane(1, 0, 0, 0),
        FPlane(0, 1, 0, 0),
        FPlane(0, 0, 0, 1)
    );

    auto CreateDesc = [&](EPixelFormat Format) -> FRDGTextureDesc {
        return FRDGTextureDesc::Create2DArray(
            FIntPoint(CubemapRes, CubemapRes), Format, FClearValueBinding(FLinearColor::Transparent),
            TexCreate_RenderTargetable | TexCreate_ShaderResource | TexCreate_UAV | TexCreate_TargetArraySlicesIndependently,
            ArraySlices
        );
        };

    FCapturedProbes Probes;
    Probes.Albedo = GraphBuilder.CreateTexture(CreateDesc(PF_R8G8B8A8), TEXT("TexelSplat_Albedo"));
    Probes.Normal = GraphBuilder.CreateTexture(CreateDesc(PF_R8G8B8A8), TEXT("TexelSplat_Normal"));
    Probes.Radial = GraphBuilder.CreateTexture(CreateDesc(PF_R32_FLOAT), TEXT("TexelSplat_Radial"));
    Probes.Eid = GraphBuilder.CreateTexture(CreateDesc(PF_R32_UINT), TEXT("TexelSplat_Eid"));

    CapturedProbesMap.Add(&InView, Probes);

    FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
        FIntPoint(CubemapRes, CubemapRes), PF_DepthStencil, FClearValueBinding::DepthFar,
        TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
    FRDGTextureRef DepthTex = GraphBuilder.CreateTexture(DepthDesc, TEXT("TexelSplat_FaceDepth"));

    FSceneViewFamily::ConstructionValues FamilyCV(InView.Family->RenderTarget, InView.Family->Scene, InView.Family->EngineShowFlags);
    FamilyCV.SetTime(InView.Family->Time);
    FSceneViewFamily* FaceFamily = GraphBuilder.AllocObject<FSceneViewFamily>(FamilyCV);
    FScene* Scene = (FScene*)InView.Family->Scene;

    for (int ProbeIdx = 0; ProbeIdx < 3; ++ProbeIdx)
    {
        for (int FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
        {
            int SliceIndex = ProbeIdx * 6 + FaceIdx;
            FVector Origin = ProbeOrigins[ProbeIdx];

            FSceneViewInitOptions ViewInitOptions;
            ViewInitOptions.ViewFamily = FaceFamily;
            ViewInitOptions.SetViewRectangle(FIntRect(0, 0, CubemapRes, CubemapRes));
            ViewInitOptions.ViewOrigin = Origin;
            ViewInitOptions.ViewRotationMatrix = FInverseRotationMatrix(FaceRotations[FaceIdx]) * ViewAxisSwap;
            ViewInitOptions.ProjectionMatrix = FReversedZPerspectiveMatrix(PI / 4.0f, 1.0f, 1.0f, 5.0f);

            FViewInfo* FaceView = GraphBuilder.AllocObject<FViewInfo>(ViewInitOptions);
            FViewUniformShaderParameters FaceViewParams = *MainViewInfo->CachedViewUniformShaderParameters;

            FaceView->SetupCommonViewUniformBufferParameters(
                FaceViewParams, FIntPoint(CubemapRes, CubemapRes), 1, FIntRect(0, 0, CubemapRes, CubemapRes),
                FaceView->ViewMatrices, FaceView->ViewMatrices);

            TUniformBufferRef<FViewUniformShaderParameters> FaceViewUB = TUniformBufferRef<FViewUniformShaderParameters>::CreateUniformBufferImmediate(FaceViewParams, UniformBuffer_SingleFrame);

            auto* PassParameters = GraphBuilder.AllocParameters<FTexelSplatCapturePassParameters>();
            PassParameters->View = FaceViewUB;
            PassParameters->InstanceCulling = FInstanceCullingContext::CreateDummyInstanceCullingUniformBuffer(GraphBuilder);
            PassParameters->Scene = GetSceneUB(MainViewInfo, GraphBuilder);

            PassParameters->RenderTargets[0] = FRenderTargetBinding(Probes.Albedo, ERenderTargetLoadAction::EClear, 0, SliceIndex);
            PassParameters->RenderTargets[1] = FRenderTargetBinding(Probes.Normal, ERenderTargetLoadAction::EClear, 0, SliceIndex);
            PassParameters->RenderTargets[2] = FRenderTargetBinding(Probes.Radial, ERenderTargetLoadAction::EClear, 0, SliceIndex);
            PassParameters->RenderTargets[3] = FRenderTargetBinding(Probes.Eid, ERenderTargetLoadAction::EClear, 0, SliceIndex);
            PassParameters->RenderTargets.DepthStencil = FDepthStencilBinding(DepthTex,
                ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear, FExclusiveDepthStencil::DepthWrite_StencilWrite);

            GraphBuilder.AddPass(
                RDG_EVENT_NAME("TexelSplat_Capture_P%d_F%d", ProbeIdx, FaceIdx),
                PassParameters,
                ERDGPassFlags::Raster,
                [Scene, MainViewInfo, FaceViewUB, CubemapRes, this](FRHICommandList& RHICmdList)
                {
                    RHICmdList.SetViewport(0, 0, 0.0f, CubemapRes, CubemapRes, 1.0f);

                    FViewInfo* MutableMainView = const_cast<FViewInfo*>(MainViewInfo);
                    TUniformBufferRef<FViewUniformShaderParameters> OriginalUB = MutableMainView->ViewUniformBuffer;
                    MutableMainView->ViewUniformBuffer = FaceViewUB;

                    DrawDynamicMeshPass(
                        *MutableMainView,
                        RHICmdList,
                        [&](FDynamicPassMeshDrawListContext* DynamicMeshPassContext)
                        {
                            FTexelSplatCaptureMeshProcessor MeshProcessor(Scene, MutableMainView, DynamicMeshPassContext, FallbackMaterialProxy);

                            for (auto It = Scene->Primitives.CreateConstIterator(); It; ++It)
                            {
                                FPrimitiveSceneInfo* PrimitiveSceneInfo = *It;
                                if (PrimitiveSceneInfo && PrimitiveSceneInfo->Proxy && PrimitiveSceneInfo->StaticMeshes.Num() > 0)
                                {
                                    for (int32 MeshIndex = 0; MeshIndex < PrimitiveSceneInfo->StaticMeshes.Num(); MeshIndex++)
                                    {
                                        const FStaticMeshBatch& StaticMesh = PrimitiveSceneInfo->StaticMeshes[MeshIndex];
                                        if (!StaticMesh.MaterialRenderProxy) continue;
                                        if (StaticMesh.MaterialRenderProxy == FallbackMaterialProxy) continue;
                                        MeshProcessor.AddMeshBatch(StaticMesh, ~0ull, PrimitiveSceneInfo->Proxy, StaticMesh.Id);
                                    }
                                }
                            }
                        }
                    );

                    MutableMainView->ViewUniformBuffer = OriginalUB;
                }
            );
        }
    }
}

void FTexelSplatViewExtension::PrePostProcessPass_RenderThread(
    FRDGBuilder& GraphBuilder,
    const FSceneView& View,
    const FPostProcessingInputs& Inputs)
{
    if (!View.bIsViewInfo || !Inputs.SceneTextures) return;

    FRDGTextureRef SceneColorTex = (*Inputs.SceneTextures)->SceneColorTexture;
    if (!SceneColorTex) return;

    FIntPoint Extent = SceneColorTex->Desc.Extent;
    const int32 CubemapRes = 384;
    const int32 ArraySlices = 18;

    FCapturedProbes Probes;
    if (FCapturedProbes* Found = CapturedProbesMap.Find(&View))
    {
        Probes = *Found;
        CapturedProbesMap.Remove(&View);
    }
    else return;

    FRDGTextureDesc ProbeLitDesc = FRDGTextureDesc::Create2DArray(
        FIntPoint(CubemapRes, CubemapRes), PF_R8G8B8A8, FClearValueBinding::None,
        TexCreate_ShaderResource | TexCreate_RenderTargetable | TexCreate_UAV | TexCreate_TargetArraySlicesIndependently,
        ArraySlices
    );
    FRDGTextureRef ProbeLitTex = GraphBuilder.CreateTexture(ProbeLitDesc, TEXT("TexelSplat_ProbeLitOut"));
    FRDGTextureUAVRef ProbeLitUAV = GraphBuilder.CreateUAV(ProbeLitTex);

    FIntVector DispatchCount(FMath::DivideAndRoundUp(CubemapRes, 8), FMath::DivideAndRoundUp(CubemapRes, 8), ArraySlices);

    TShaderMapRef<FTexelSplatShadingCS> ShadingShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    FTexelSplatShadingCS::FParameters* ShadingParams = GraphBuilder.AllocParameters<FTexelSplatShadingCS::FParameters>();

    ShadingParams->ProbeAlbedoIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Probes.Albedo));
    ShadingParams->ProbeNormalIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Probes.Normal));
    ShadingParams->ProbeRadialIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Probes.Radial));
    ShadingParams->ProbeLitOut = ProbeLitUAV;

    FComputeShaderUtils::AddPass(GraphBuilder, RDG_EVENT_NAME("TexelSplat_ShadingProbes"), ShadingShader, ShadingParams, DispatchCount);

    FSplatPassParameters* SplatParams = GraphBuilder.AllocParameters<FSplatPassParameters>();

    SplatParams->ScreenToWorld = FMatrix44f(View.ViewMatrices.GetInvViewProjectionMatrix());
    SplatParams->WorldToClip = FMatrix44f(View.ViewMatrices.GetTranslatedViewProjectionMatrix());

    FVector3f CamPos = (FVector3f)View.ViewMatrices.GetViewOrigin();
    SplatParams->CameraPos = FVector4f(CamPos.X, CamPos.Y, CamPos.Z, 0.0f);
    SplatParams->GridOrigin = FVector4f(CurrentGridOrigin.X, CurrentGridOrigin.Y, CurrentGridOrigin.Z, 0.0f);
    SplatParams->PrevOrigin = FVector4f(PrevGridOrigin.X, PrevGridOrigin.Y, PrevGridOrigin.Z, 0.0f);

    FVector3f PreTrans = (FVector3f)View.ViewMatrices.GetPreViewTranslation();
    SplatParams->PreViewTranslation = FVector4f(PreTrans.X, PreTrans.Y, PreTrans.Z, 0.0f);

    SplatParams->CrossfadeTimer = CrossfadeTimer;

    SplatParams->ProbeLitIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(ProbeLitTex));
    SplatParams->ProbeRadialIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Probes.Radial));
    SplatParams->ProbeNormalIn = GraphBuilder.CreateSRV(FRDGTextureSRVDesc::Create(Probes.Normal));

    FRDGTextureDesc DepthDesc = FRDGTextureDesc::Create2D(
        Extent, PF_DepthStencil, FClearValueBinding::DepthFar,
        TexCreate_DepthStencilTargetable | TexCreate_ShaderResource);
    FRDGTextureRef DepthTex = GraphBuilder.CreateTexture(DepthDesc, TEXT("TexelSplat_Depth"));

    SplatParams->RenderTargets[0] = FRenderTargetBinding(SceneColorTex, ERenderTargetLoadAction::ELoad);
    SplatParams->RenderTargets.DepthStencil = FDepthStencilBinding(
        DepthTex,
        ERenderTargetLoadAction::EClear, ERenderTargetLoadAction::EClear,
        FExclusiveDepthStencil::DepthWrite_StencilNop);

    const FViewInfo* ViewInfo = (FViewInfo*)&View;
    FIntRect ViewRect = ViewInfo->ViewRect;

    TShaderMapRef<FTexelSplatVS> VertexShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));
    TShaderMapRef<FTexelSplatPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

    GraphBuilder.AddPass(
        RDG_EVENT_NAME("TexelSplat_SplatPass"), SplatParams, ERDGPassFlags::Raster,
        [SplatParams, VertexShader, PixelShader, ViewRect, CubemapRes, ArraySlices](FRHICommandList& RHICmdList)
        {
            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_GreaterEqual>::GetRHI();
            GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;
            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), *SplatParams);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), *SplatParams);

            RHICmdList.SetViewport(ViewRect.Min.X, ViewRect.Min.Y, 0.0f, ViewRect.Max.X, ViewRect.Max.Y, 1.0f);

            RHICmdList.DrawPrimitive(0, 2, CubemapRes * CubemapRes * ArraySlices);
        });
}