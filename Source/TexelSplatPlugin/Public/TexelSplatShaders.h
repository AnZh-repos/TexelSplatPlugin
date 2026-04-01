#pragma once

#include "GlobalShader.h"
#include "ShaderParameterStruct.h"
#include "MeshMaterialShader.h"

// --- Pass 1: Shading ---
class FTexelSplatShadingCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FTexelSplatShadingCS);
    SHADER_USE_PARAMETER_STRUCT(FTexelSplatShadingCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, ProbeAlbedoIn)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, ProbeNormalIn)
        SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ProbeRadialIn)
        SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture2DArray<float4>, ProbeLitOut)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

// --- Pass 2: Splatting (Raster) ---
BEGIN_SHADER_PARAMETER_STRUCT(FSplatPassParameters, )
    SHADER_PARAMETER(FMatrix44f, ScreenToWorld)
    SHADER_PARAMETER(FMatrix44f, WorldToClip)
    SHADER_PARAMETER(FVector4f, CameraPos)
    SHADER_PARAMETER(FVector4f, GridOrigin)
    SHADER_PARAMETER(FVector4f, PrevOrigin)
    SHADER_PARAMETER(FVector4f, PreViewTranslation)
    SHADER_PARAMETER(float, CrossfadeTimer)
    SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, ProbeLitIn)
    SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float>, ProbeRadialIn)
    SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture2DArray<float4>, ProbeNormalIn)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

class FTexelSplatVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FTexelSplatVS);
    SHADER_USE_PARAMETER_STRUCT(FTexelSplatVS, FGlobalShader);
    using FParameters = FSplatPassParameters;
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FTexelSplatPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FTexelSplatPS);
    SHADER_USE_PARAMETER_STRUCT(FTexelSplatPS, FGlobalShader);
    using FParameters = FSplatPassParameters;
    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters) { return true; }
};

class FTexelSplatCaptureVS : public FMeshMaterialShader
{
public:
    DECLARE_SHADER_TYPE(FTexelSplatCaptureVS, MeshMaterial);
    FTexelSplatCaptureVS() {}
    FTexelSplatCaptureVS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters) { return Parameters.MaterialParameters.MaterialDomain == MD_Surface; }
};

class FTexelSplatCapturePS : public FMeshMaterialShader
{
public:
    DECLARE_SHADER_TYPE(FTexelSplatCapturePS, MeshMaterial);
    FTexelSplatCapturePS() {}
    FTexelSplatCapturePS(const ShaderMetaType::CompiledShaderInitializerType& Initializer) : FMeshMaterialShader(Initializer) {}
    static bool ShouldCompilePermutation(const FMeshMaterialShaderPermutationParameters& Parameters) { return Parameters.MaterialParameters.MaterialDomain == MD_Surface; }
};