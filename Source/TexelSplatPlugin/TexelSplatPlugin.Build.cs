using UnrealBuildTool;

public class TexelSplatPlugin : ModuleRules
{
    public TexelSplatPlugin(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
        });


        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "CoreUObject",
            "Engine",
            "Renderer",
            "RenderCore",
            "RHI",
            "Projects"
        });


        PrivateIncludePaths.AddRange(new string[] {
            System.IO.Path.Combine(ModuleDirectory, "Private"),
            System.IO.Path.Combine(GetModuleDirectory("RenderCore"), "Private"),
			System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Private"),
            System.IO.Path.Combine(GetModuleDirectory("Renderer"), "Internal"),
        });
    }
}