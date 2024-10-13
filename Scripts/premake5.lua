-- Cyclone
workspace "Cyclone"
    architecture "x64"
    startproject "Cyclone"
    location "../"          -- Premake files are generated in the same directory as this script. Hence, redirect.

    configurations
    {
        "Debug",
        "Release"
    }

    flags
    {
        "MultiProcessorCompile"
    }

    defines
    {
        "NOMINMAX"
    }

IncludeDirectories = {}
IncludeDirectories["GLM"] = "%{wks.location}Dependencies/GLM"

include "../Cyclone"