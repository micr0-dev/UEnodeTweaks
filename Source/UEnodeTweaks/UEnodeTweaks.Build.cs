using UnrealBuildTool;

public class UEnodeTweaks : ModuleRules
{
    public UEnodeTweaks(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Slate",
            "SlateCore",
            "UnrealEd",
            "GraphEditor",
            "BlueprintGraph",
            "InputCore",
            "DeveloperSettings",
            "KismetCompiler",
        });

    }
}
