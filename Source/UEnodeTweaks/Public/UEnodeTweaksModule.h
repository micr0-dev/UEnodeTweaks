#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FMultiConnectPreprocessor;

class FUEnodeTweaksModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedPtr<FMultiConnectPreprocessor> InputPreprocessor;
};
