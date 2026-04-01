#include "TexelSplatShaders.h"

IMPLEMENT_GLOBAL_SHADER(FTexelSplatShadingCS, "/TexelSplatPlugin/TexelSplat.usf", "ShadingCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FTexelSplatVS, "/TexelSplatPlugin/TexelSplat.usf", "SplatVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FTexelSplatPS, "/TexelSplatPlugin/TexelSplat.usf", "SplatPS", SF_Pixel);

IMPLEMENT_MATERIAL_SHADER_TYPE(, FTexelSplatCaptureVS, TEXT("/TexelSplatPlugin/TexelSplatCapture.usf"), TEXT("MainVS"), SF_Vertex);
IMPLEMENT_MATERIAL_SHADER_TYPE(, FTexelSplatCapturePS, TEXT("/TexelSplatPlugin/TexelSplatCapture.usf"), TEXT("MainPS"), SF_Pixel);