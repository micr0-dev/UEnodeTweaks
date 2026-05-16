#include "UEnodeTweaksModule.h"
#include "MultiConnectPreprocessor.h"
#include "MultiPinDragPreprocessor.h"

#include "Framework/Application/SlateApplication.h"
#include "Modules/ModuleManager.h"

void FUEnodeTweaksModule::StartupModule()
{
    if (FSlateApplication::IsInitialized())
    {
        MultiConnectProcessor = MakeShared<FMultiConnectPreprocessor>();
        MultiPinDragProcessor = MakeShared<FMultiPinDragPreprocessor>();
        // MultiPinDrag runs first (priority 0) so it can block MultiConnect
        // (priority 1) when a multi-pin selection is active, preventing
        // duplicate reconnect handling on the same drop event.
        FSlateApplication::Get().RegisterInputPreProcessor(MultiPinDragProcessor, 0);
        FSlateApplication::Get().RegisterInputPreProcessor(MultiConnectProcessor, 1);
    }
}

void FUEnodeTweaksModule::ShutdownModule()
{
    if (FSlateApplication::IsInitialized())
    {
        if (MultiConnectProcessor.IsValid())
            FSlateApplication::Get().UnregisterInputPreProcessor(MultiConnectProcessor);
        if (MultiPinDragProcessor.IsValid())
            FSlateApplication::Get().UnregisterInputPreProcessor(MultiPinDragProcessor);
    }
    MultiConnectProcessor.Reset();
    MultiPinDragProcessor.Reset();
}

IMPLEMENT_MODULE(FUEnodeTweaksModule, UEnodeTweaks)
