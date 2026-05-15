using UnrealBuildTool;
using System.IO;

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
            "EditorSubsystem",
            "InputCore",
        });

        // Access FDragConnection from GraphEditor's private headers
        PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source/Editor/GraphEditor/Private"));
    }
}
