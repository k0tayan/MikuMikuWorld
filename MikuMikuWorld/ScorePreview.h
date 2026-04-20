#pragma once
#include "ScoreContext.h"
#include "ScoreEditorTimeline.h"
#include "Preview/Overlay.h"
#include "Rendering/Framebuffer.h"
#include "Rendering/Renderer.h"
#include "Rendering/Camera.h"

namespace MikuMikuWorld
{
	class ScorePreviewBackground
	{
		std::string backgroundFile;
		std::string jacketFile;
		Framebuffer frameBuffer;
		float brightness;
		bool init;
		
		struct DefaultJacket
		{
			static std::array<DirectX::XMFLOAT4, 4> getLeftUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightUV();
			static std::array<DirectX::XMFLOAT4, 4> getLeftMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getRightMirrorUV();
			static std::array<DirectX::XMFLOAT4, 4> getCenterUV();
			static std::array<DirectX::XMFLOAT4, 4> getMirrorCenterUV();
		};
		void updateDrawDefaultJacket(Renderer* renderer, const Jacket& jacket);
		public:
		ScorePreviewBackground();
		~ScorePreviewBackground();

		void setBrightness(float value);
		void update(Renderer* renderer, const Jacket& jacket);
		bool shouldUpdate(const Jacket& jacket) const;
		void draw(Renderer* renderer, float scrWidth, float scrHeight) const;		
	};

	class ScorePreviewWindow
	{
		Framebuffer previewBuffer;
		ScorePreviewBackground background;
		float scaledAspectRatio;

		Overlay overlay;
		bool overlayInitAttempted{ false };
		bool lastPlayingState{ false };
		int  lastOverlayScoreRevision{ -1 };
		std::string lastIntroFontPath;

		/// <summary>
		/// The camera used to align particles to preview
		/// </summary>
		Camera noteEffectsCamera;
		
		/// <summary>
		/// The camera used to billboard effects
		/// </summary>
		Camera mainCamera;

		mutable bool fullWindow{};

		const Texture& getNoteTexture();

		void drawNoteBase(Renderer* renderer, const Note& note, float left, float right, float y, float zScalar = 1);
		void drawTraceDiamond(Renderer* renderer, const Note& note, float left, float right, float y);
		void drawFlickArrow(Renderer* renderer, const Note& note, float y, double cur_time);

		void drawStageCoverMask(Renderer* renderer);
		
		void updateToolbar(ScoreEditorTimeline& timeline, ScoreContext& context) const;
		float getScrollbarWidth() const;
		void updateScrollbar(ScoreEditorTimeline& timeline, ScoreContext& context) const;
	public:
		ScorePreviewWindow();
		~ScorePreviewWindow();
		void update(ScoreContext& context, Renderer* renderer, ScoreEditorTimeline& timeline);
		void updateUI(ScoreEditorTimeline& timeline, ScoreContext& context);

		// Render pure GL contents of the preview to the internal framebuffer.
		// Used by both the ImGui preview window and the offline video renderer.
		void renderToFramebuffer(ScoreContext& context, Renderer* renderer,
		                        float viewportWidth, float viewportHeight,
		                        float currentTime, bool isPlaying);
		Framebuffer& getPreviewBuffer() { return previewBuffer; }

		void drawNotes(const ScoreContext& context, Renderer* renderer);
		void drawLines(const ScoreContext& context, Renderer* renderer);
		void drawHoldTicks(const ScoreContext& context, Renderer* renderer);
		void drawHoldCurves(const ScoreContext& context, Renderer* renderer);

		void drawStage(Renderer* renderer);
		void drawStageCover(Renderer* renderer);
		void drawStageCoverDecoration(Renderer* renderer);

		void setFullWindow(bool fullScreen);

		inline bool isFullWindow() const { return fullWindow; };

		void loadNoteEffects(Effect::EffectView& effectView);

		// Offline-render hook: configure the pre-chart intro animation. Must be
		// called before renderToFramebuffer so overlay.init() has already run.
		void configureIntro(float offsetSeconds, const OverlayIntroData& data);
	};
}