#pragma once

#include <cstdint>

#define NEXUS_API_VERSION 6

typedef struct Texture_t
{
	std::uint32_t Width;
	std::uint32_t Height;
	void* Resource;
} Texture_t;

typedef Texture_t* (*TEXTURES_GETORCREATEFROMURL)(const char* aIdentifier, const char* aRemote, const char* aEndpoint);

typedef struct AddonAPI_t
{
	void* ImguiContext;
	void* ImguiMalloc;
	void* ImguiFree;
	TEXTURES_GETORCREATEFROMURL Textures_GetOrCreateFromURL;
} AddonAPI_t;
