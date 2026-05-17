#pragma once

#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"

class FMultiConnectPreprocessor;
class FMultiPinDragPreprocessor;
struct FGraphPanelPinConnectionFactory;
struct FGraphPanelNodeFactory;

class FUEnodeTweaksModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    TSharedPtr<FMultiConnectPreprocessor>       MultiConnectProcessor;
    TSharedPtr<FMultiPinDragPreprocessor>       MultiPinDragProcessor;
    TSharedPtr<FGraphPanelPinConnectionFactory> ConnectionFactory;
    TSharedPtr<FGraphPanelNodeFactory>          NodeFactory;
};
