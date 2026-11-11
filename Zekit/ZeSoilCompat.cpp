#include "pch.h"

#if !defined(_WIN32)

#include <SOIL2/SOIL2.h>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif
#define STB_IMAGE_STATIC
#define STBI_NO_PKM
#define STB_IMAGE_IMPLEMENTATION
#include <SOIL2/stb_image.h>
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <string>

namespace {

	std::string g_lastResult = "unknown";

	void SetResult(std::string text) { g_lastResult = std::move(text); }

}

extern "C" {

	unsigned char* SOIL_load_image(const char* filename,
		int* width, int* height, int* channels, int force_channels) {
		if (width) *width = 0;
		if (height) *height = 0;
		if (channels) *channels = 0;

		if (!filename || !*filename) {
			SetResult("empty filename");
			return nullptr;
		}

		int req = force_channels;
		if (req < 0 || req > 4) req = 0;

		int w = 0, h = 0, originalChannels = 0;
		unsigned char* pixels = stbi_load(filename, &w, &h, &originalChannels, req);
		if (!pixels) {
			const char* why = stbi_failure_reason();
			SetResult(std::string("stb_image: ") + (why ? why : "unknown"));
			return nullptr;
		}

		if (width) *width = w;
		if (height) *height = h;
		if (channels) *channels = originalChannels;
		SetResult("OK");
		return pixels;
	}

	unsigned char* SOIL_load_image_from_memory(const unsigned char* const buffer,
		int buffer_length, int* width, int* height, int* channels, int force_channels) {
		if (width) *width = 0;
		if (height) *height = 0;
		if (channels) *channels = 0;

		if (!buffer || buffer_length <= 0) {
			SetResult("empty buffer");
			return nullptr;
		}

		int req = force_channels;
		if (req < 0 || req > 4) req = 0;

		int w = 0, h = 0, originalChannels = 0;
		unsigned char* pixels = stbi_load_from_memory(buffer, buffer_length,
			&w, &h, &originalChannels, req);
		if (!pixels) {
			const char* why = stbi_failure_reason();
			SetResult(std::string("stb_image: ") + (why ? why : "unknown"));
			return nullptr;
		}

		if (width) *width = w;
		if (height) *height = h;
		if (channels) *channels = originalChannels;
		SetResult("OK");
		return pixels;
	}

	void SOIL_free_image_data(unsigned char* img_data) {
		stbi_image_free(img_data);
	}
	const char* SOIL_last_result(void) {
		return g_lastResult.c_str();
	}

}

#endif
