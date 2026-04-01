#include "TexelSplatPlugin.h"
#include "TexelSplatViewExtension.h"
#include "Interfaces/IPluginManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY(LogTexelSplat);
#define LOCTEXT_NAMESPACE "FTexelSplatPluginModule"

void FTexelSplatPluginModule::StartupModule()
{
	// Map the virtual shader path
	FString PluginShaderDir = FPaths::Combine(IPluginManager::Get().FindPlugin(TEXT("TexelSplatPlugin"))->GetBaseDir(), TEXT("Shaders"));
	AddShaderSourceDirectoryMapping(TEXT("/TexelSplatPlugin"), PluginShaderDir);

	FCoreDelegates::OnPostEngineInit.AddLambda([this]()
		{
			ViewExtension = FSceneViewExtensions::NewExtension<FTexelSplatViewExtension>();
			UE_LOG(LogTexelSplat, Log, TEXT("Plugin started. SVE registered."));
		});
}

void FTexelSplatPluginModule::ShutdownModule()
{
	ViewExtension.Reset();
	UE_LOG(LogTexelSplat, Log, TEXT("Plugin shutdown. SVE released."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FTexelSplatPluginModule, TexelSplatPlugin)