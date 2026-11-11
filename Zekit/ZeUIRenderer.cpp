#include "pch.h"

#include "ZeUIRenderer.h"
#include "ZeFrameCollector.h"
#include "ZeGLProgram.h"
#include "include/ZeCanvasShaders.h"

#include "ZeGLApi.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <map>
#include <string>

namespace Kit {
	namespace {

#if ZEKIT_GL_IS_GLES
		constexpr const char* kFsPrelude = "precision mediump float;\n";
#else
		constexpr const char* kFsPrelude = "";
		constexpr const char* kGlslLadder[] = {
			"#version 460 core\n",
			"#version 450 core\n",
			"#version 440 core\n",
			"#version 430 core\n",
			"#version 410 core\n",
			"#version 330 core\n",
		};
#endif

		int GlslStartIndex() {
#if ZEKIT_GL_IS_GLES
			return 0;
#else
			const char* s = reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
			if (!s) return 0;
			int major = 0, minor = 0;
			if (std::sscanf(s, "%d.%d", &major, &minor) != 2) return 0;
			const int ver = major * 100 + minor * 10;
			const int ladder[] = { 460, 450, 440, 430, 410, 330 };
			for (int i = 0; i < 6; ++i) {
				if (ver >= ladder[i]) return i;
			}
			return 5;
#endif
		}

		void BuildShaderPair(const char* versionLine, std::string& vs, std::string& fs) {
			vs = std::string(versionLine) + CanvasShaders::kVertexGlsl;
			fs = std::string(versionLine) + kFsPrelude + CanvasShaders::kFragmentGlsl;
		}

		void DumpFrameIfAsked(int W, int H) {
			static const char* prefix = std::getenv("ZEKIT_DUMP_PIXELS");
			if (!prefix || !*prefix || W <= 0 || H <= 0) return;
			static std::map<long long, int> dumped;
			const long long key = (static_cast<long long>(W) << 32) | static_cast<unsigned>(H);
			int& n = dumped[key];
			const int slot = n < 5 ? n : 5;
			++n;
			if (slot == 5 && (n % 15) != 0) return;

			std::vector<unsigned char> rgba(static_cast<size_t>(W) * H * 4);
			glPixelStorei(GL_PACK_ALIGNMENT, 1);
			glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
			glPixelStorei(GL_PACK_ALIGNMENT, 4);

			char path[512];
			std::snprintf(path, sizeof path, "%s_%dx%d_%02d.ppm", prefix, W, H, slot);
			FILE* f = std::fopen(path, "wb");
			if (!f) return;
			std::fprintf(f, "P6\n%d %d\n255\n", W, H);
			for (int y = H - 1; y >= 0; --y) {
				const unsigned char* row = rgba.data() + static_cast<size_t>(y) * W * 4;
				for (int x = 0; x < W; ++x) std::fwrite(row + x * 4, 1, 3, f);
			}
			std::fclose(f);
		}
	}

	UIRenderer::~UIRenderer() { Shutdown(); };

	bool UIRenderer::Init() {
		if (prog_) return true;

		std::string vs, fs;
#if ZEKIT_GL_IS_GLES
		BuildShaderPair("#version 300 es\n", vs, fs);
		prog_ = GL::CompileProgram(vs.c_str(), fs.c_str());
#else
		const int total  = static_cast<int>(sizeof(kGlslLadder) / sizeof(kGlslLadder[0]));
		int       first  = GlslStartIndex();
		if (first < 0 || first >= total) first = 0;
		for (int i = first; i < total && !prog_; ++i) {
			BuildShaderPair(kGlslLadder[i], vs, fs);
			prog_ = GL::CompileProgram(vs.c_str(), fs.c_str());
			if (prog_ && i != 0) {
				std::cerr << "[GL] GLSL 降档到 " << kGlslLadder[i];
			}
		}
#endif
		if (!prog_) {
			std::cerr << "[GL] 所有候选 GLSL 版本都编不过 —— 这台机器的 GL 驱动不满足本库的最低要求"
			             "（需要 GLSL 3.30 core 以上）\n";
			return false;
		}

		locProj_ = glGetUniformLocation(prog_, "uProj");
		locTex_ = glGetUniformLocation(prog_, "uTex");
		locIsTex_ = glGetUniformLocation(prog_, "uIsTex");
		locIsColor_ = glGetUniformLocation(prog_, "uIsColor");

		glGenVertexArrays(1, &vao_);
		glGenBuffers(1, &vbo_);
		glBindVertexArray(vao_);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
			(void*)(2 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
			(void*)(6 * sizeof(float)));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
			(void*)(8 * sizeof(float)));
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(UIVertex),
			(void*)(12 * sizeof(float)));
		glBindVertexArray(0);
		return true;
	}

	void UIRenderer::Shutdown() {
		if (vbo_) { glDeleteBuffers(1, &vbo_);      vbo_ = 0; }
		if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
		if (prog_) { glDeleteProgram(prog_);         prog_ = 0; }
		locProj_ = locTex_ = locIsTex_ = locIsColor_ = -1;
		vboCapacity_ = 0;
	}

	void UIRenderer::Draw(const FrameCollector& fc, int W, int H) {
		if (!prog_ || fc.Empty()) return;

		const glm::mat4 proj = glm::ortho(0.f, (float)W, (float)H, 0.f, -1.f, 1.f);

		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
#if defined(GL_MULTISAMPLE)
		glEnable(GL_MULTISAMPLE);
#endif
		glUseProgram(prog_);
		glUniformMatrix4fv(locProj_, 1, GL_FALSE, glm::value_ptr(proj));
		glUniform1i(locTex_, 0);

		glBindVertexArray(vao_);
		glBindBuffer(GL_ARRAY_BUFFER, vbo_);

		const GLsizeiptr bytes = (GLsizeiptr)(fc.Vertices().size() * sizeof(UIVertex));
		if (bytes > vboCapacity_) {
			glBufferData(GL_ARRAY_BUFFER, bytes, fc.Vertices().data(), GL_DYNAMIC_DRAW);
			vboCapacity_ = bytes;
		}
		else {
			glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, fc.Vertices().data());
		}

		const uint32_t* idxBase = fc.Indices().data();

		for (const auto& b : fc.Batches()) {
			if (b.texture != 0) {
				glActiveTexture(GL_TEXTURE0);
				glBindTexture(GL_TEXTURE_2D, b.texture);
				glUniform1i(locIsTex_, 1);
				if (locIsColor_ >= 0)
					glUniform1i(locIsColor_, b.isColor ? 1 : 0);
			}
			else {
				glUniform1i(locIsTex_, 0);
			}
			glDrawElements(GL_TRIANGLES, (GLsizei)b.indexCount,
				GL_UNSIGNED_INT, idxBase + b.indexOffset);
		}

		glBindVertexArray(0);
		glDisable(GL_BLEND);

		DumpFrameIfAsked(W, H);
	}

}
