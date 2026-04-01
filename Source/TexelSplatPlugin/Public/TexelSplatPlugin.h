#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogTexelSplat, Log, All);

class FTexelSplatViewExtension;

class FTexelSplatPluginModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedPtr<FTexelSplatViewExtension, ESPMode::ThreadSafe> ViewExtension;
};
