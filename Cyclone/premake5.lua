project "Cyclone"
    kind "ConsoleApp"
    language "C++"
    cppdialect "C++17"
    staticruntime "off"
    warnings "Extra"
    location ("%{wks.location}Cyclone")

    targetdir   ("%{wks.location}Binaries/%{prj.name}/%{cfg.buildcfg}/Output")
    objdir      ("%{wks.location}Binaries/%{prj.name}/%{cfg.buildcfg}/Intermediates")

    files
    {
        "**.h",
        "**.cpp",
    }

    includedirs
    {
        "Core",
        "%{IncludeDirectories.GLM}",
    }

    filter "configurations:Debug"
        runtime "Debug"
        optimize "Off"
        symbols "On"

    filter "configurations:Release"
        runtime "Release"
        optimize "On"
        symbols "On"