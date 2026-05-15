#include "UEnodeTweaksModule.h"
#include "MultiConnectPreprocessor.h"

#include "Application/SlateApplication.h"
#include "Modules/ModuleManager.h"

void FUEnodeTweaksModule::StartupModule()
{
    if (FSlateApplication::IsInitialized())
    {
        InputPreprocessor = MakeShared<FMultiConnectPreprocessor>();
        // Priority 0 = runs before all other input processors
        FSlateApplication::Get().RegisterInputPreProcessor(InputPreprocessor, 0);
    }
}

void FUEnodeTweaksModule::ShutdownModule()
{
    if (FSlateApplication::IsInitialized() && InputPreprocessor.IsValid())
    {
        FSlateApplication::Get().UnregisterInputPreProcessor(InputPreprocessor);
    }
    InputPreprocessor.Reset();
}

IMPLEMENT_MODULE(FUEnodeTweaksModule, UEnodeTweaks)
