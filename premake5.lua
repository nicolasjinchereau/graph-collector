require "android-lib"

workspace "graph-collector"
    configurations { "Debug", "Release" }
    location ("projects/" .. os.target())

project "graph-collector"
    kind "StaticLib"
    language "C"
    --architecture "x86_64"

    location ("projects/" .. os.target())
    targetname "graph-collector"
    
    files {
        "src/gc/gc.cpp",
        "src/gc/gc.h",
        "src/gc/detail/compressed_pair.h",
        "src/gc/detail/functor.h",
        "src/gc/detail/intrusive_list.h",
        "src/gc/detail/vector.h"
    }

    externalincludedirs {
        "src/"
    }

    androidabis {
		"armeabi-v7a", "arm64-v8a"
    }

    androidminsdkversion "android-16"

    filter { "action:vs*" }
        objdir ("!obj/$(PlatformName)/$(Configuration)")
        targetdir ("lib/$(PlatformName)/$(Configuration)")
        characterset "MBCS"

    filter { "action:xcode*" }
        objdir ("!obj/$(PLATFORM_NAME)/$(CONFIGURATION)")
        targetdir ("lib/$(PLATFORM_NAME)/$(CONFIGURATION)")

    filter { "action:android*" }
        objdir ("obj/android/$(TARGET_ARCH_ABI)/$(APP_OPTIM)")
        targetdir ("lib/android/$(TARGET_ARCH_ABI)/$(APP_OPTIM)")

    filter "configurations:Debug"
        defines {
            "WIN32",
            "NDEBUG"
        }
        symbols "On"
        runtime "Debug"
        optimize "Speed"
        inlining "Auto"
        floatingpoint "Fast"
        vectorextensions "SSE2"
        
    filter "configurations:Release"
        defines {
            "WIN32",
            "NDEBUG"
        }
        optimize "Speed"
        inlining "Auto"
        floatingpoint "Fast"
        vectorextensions "SSE2"

    filter { "action:xcode*" }
        xcodebuildsettings {           
            ["CLANG_CXX_LANGUAGE_STANDARD"] = "gnu++20";
            ["GCC_C_LANGUAGE_STANDARD"] = "gnu11";
            ["CLANG_CXX_LIBRARY"] = "libc++";
        }
    
    filter { "action:vs*" }
        cppdialect "C++latest"
        cdialect "C11"
        toolset "v143"