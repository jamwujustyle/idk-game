using UnrealBuildTool;

public class Ronin : ModuleRules
{
    public Ronin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        CppStandard = CppStandardVersion.Cpp20;
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "InputCore", "ApplicationCore"});
        PrivateDependencyModuleNames.Add("AnimGraphRuntime");
    }
}
