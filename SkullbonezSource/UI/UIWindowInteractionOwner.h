// Lifetime: every reference aliases UIWindowInteractionOwner storage for one
// synchronous draw/composition call. Consumers must never retain this view.
// Invariant: hit testing and drawing borrow the same widget instances and
// geometry, preventing presentation from diverging from pointer behavior./*
File: SkullbonezSource/UI/UIWindowInteractionOwner.h
Purpose:
  Owns the persistent window, widget, and pointer-interaction state used by the in-game UI.

Summary:
  UIWindowInteractionOwner translates normalized UI input into typed commands

  while retaining the widget geometry and gesture state that drawing consumes.
  InGameUI remains the draw/resource composer and borrows WidgetView only for
  the duration of one draw call.

Invariants:
  - Widget bounds used for hit testing and drawing come from the same objects.
  - The owner never retains an InGameUI pointer, reference, or callback.
  - WidgetView is borrowed synchronously and is never stored by a consumer.

Related:
  - SkullbonezSource/UI/UI.cpp
  - SkullbonezSource/UI/UIWindowInteractionOwner.cpp
  - Agentic/Reference/engine-glossary.md
*/
#pragma once

#include "UIBackdropBlur.h"
#include "UIButton.h"
#include "UICache.h"
#include "UICheckBox.h"
#include "UIComboBox.h"
#include "UICommands.h"
#include "UIInput.h"
#include "UIScrollBar.h"
#include "UISlider.h"
#include "UISceneNavigationModel.h"
#include "UIState.h"
#include "UITabBar.h"
#include "UITabCinematic.h"
#include "UITabControls.h"
#include "UITabEditor.h"
#include "UITabMemory.h"
#include "UITabOptions.h"
#include "UITabPhysics.h"
#include "UITabProfiler.h"
#include "UITabScene.h"
#include "UITabSky.h"
#include "UILayout.h"

#include <cstdint>
#include <span>

namespace SkullbonezCore
{
    namespace UI
    {

    enum class InGameUITab;

    class UIWindowInteractionOwner
    {
      public:
        struct WidgetView
        {
            UIWindowState& window;
            UIInteractionState& interaction;
            bool& blurPreviewEnabled;
            InGameUITab& activeTab;
            UITabBar& tabBar;
            UICheckBox& blurToggle;
            UICheckBox& vsyncToggle;
            UICheckBox& timelineToggle;
            UICheckBox& histogramToggle;
            UICheckBox& hitboxToggle;
            UIComboBox& rendererCombo;
            UIComboBox& reflectionCombo;
            UIComboBox& renderTargetCombo;
            UIComboBox& cameraModeCombo;
            UICheckBox& cinematicMasterToggle;
            UICheckBox& renderShadowToggle;
            UIButton& saveRenderDefaultsButton;
            UIButton& saveTrajectoryStyleButton;
            UISlider ( &renderSliders )[static_cast<int>( UIRenderParam::Count )];
            UIBackdropBlur& backdropBlur;
            UICacheState& cache;
            UIScrollBar& scrollBar;
            int& mouseX;
            int& mouseY;
            int& lastScreenW;
            int& lastScreenH;
            int& lastModelCapacity;
            int& lastSolverBallCount;
            int& lastSolverBoxCount;
            int& lastWorkerThreadCount;
            int& lastMaxWorkerThreadCount;
            int& lastRenderTargetPreviewCount;
            uint32_t& lastRenderTargetDisabledMask;
            int& selectedRenderTargetPreview;
            ControlsTab::UIControlsTabState& controlsTab;
            EditorTab::UIEditorTabState& editorTab;
            OptionsTab::UIOptionsTabState& optionsTab;
            PhysicsTab::UIPhysicsTabState& physicsTab;
            ProfilerTab::UIProfilerTabState& profilerTab;
            MemoryTab::UIMemoryOverlayState& memoryOverlay;
            SceneTab::UISceneTabState& sceneTab;
            SkyTab::UISkyTabState& skyTab;
            CinematicTab::UICinematicTabState& cinematicTab;
            float& scrollY;
            double& scrollbarVisibleUntil;
            int& activeSlider;
            bool& hitboxOverlayEnabled;
            bool& editorMiniPalettePressActive;
            bool& editorMiniPaletteFlyoutOpen;
            int& editorMiniPalettePressedEntry;
            int& editorMiniPalettePressedObjectType;
            int& editorMiniPalettePressedTreePlacement;
            int& editorMiniPalettePressedHoldMode;
            double& editorMiniPalettePressStart;
        };

        UIWindowInteractionOwner();

        bool IsVisible() const;
        bool IsMinimized() const;
        void SetVisible( bool visible, double now );
        void ToggleVisible( double now );
        void SetMinimized( bool minimized, double now );
        void SetActiveTab( InGameUITab tab );
        InGameUITab GetActiveTab() const;
        void CancelInputCapture();
        bool BlocksCameraMouse() const;
        bool BlocksKeyboard() const;
        bool WantsNativeMouseCursor() const;
        void SetWindowBounds( int x, int y, int width, int height );
        void SetBlurEnabled( bool enabled );
        void SetRendererComboOpen( bool open );
        void SetWaterComboOpen( bool open );
        void SetSceneComboOpen( bool open );
        void SetSceneFilter( const char* filter );
        void SetProfilerExpandAll( bool expandAll );
        void SetProfilerTimelineEnabled( bool enabled );
        void SetPerformanceHistogramEnabled( bool enabled );
        bool IsPerformanceHistogramEnabled() const;
        void TogglePerformanceHistogramEnabled();
        void SetMemoryOverlayEnabled( bool enabled );
        bool IsMemoryOverlayEnabled() const;
        void ToggleMemoryOverlayEnabled();
        bool NeedsUiTextPass() const;
        void SetHitboxOverlayEnabled( bool enabled );
        void SetScrollY( float scrollY );
        void SetMouseOverride( bool enabled, int x, int y );
        void ResetPresentationResources();
        int ContentHeight() const;
        WidgetView Widgets();

        // Returns the optional deterministic pointer substitution as a detached
        // value; Runtime applies it while copying the sampled input snapshot.
        InputControl::UIPointerOverride InputOverride() const;

        // Consumes one normalized input turn and explicit presentation facts. Scene
        // and runtime mutations are returned as commands rather than applied here.
        InGameUIInputResult UpdateInput( const InputControl::UIInputSnapshot& input, int screenWidth, int screenHeight,
                                         double now, bool editorModeEnabled, bool editorPlacementMode,
                                         bool editorPlaceStatic, bool editorTerrainAlign, int cameraModeIndex,
                                         uint32_t cameraModeEnabledMask, std::span<const char* const> sceneOptions,
                                         int selectedSceneOption );

      private:
        void CloseSceneCombo();
        void CancelEditorMiniPaletteInteraction();
        void SetMaximized( bool maximized, int screenW, int screenH, double now );

        UIWindowState m_window;
        UIInteractionState m_interaction;
        bool m_blurPreviewEnabled = false;
        InGameUITab m_activeTab;
        UITabBar m_tabBar;
        UICheckBox m_blurToggle;
        UICheckBox m_vsyncToggle;
        UICheckBox m_timelineToggle;
        UICheckBox m_histogramToggle;
        UICheckBox m_hitboxToggle;
        UIComboBox m_rendererCombo;
        UIComboBox m_reflectionCombo;
        UIComboBox m_renderTargetCombo;
        UIComboBox m_cameraModeCombo;
        UICheckBox m_cinematicMasterToggle;
        UICheckBox m_renderShadowToggle;
        UIButton m_saveRenderDefaultsButton;
        UIButton m_saveTrajectoryStyleButton;
        UISlider m_renderSliders[static_cast<int>( UIRenderParam::Count )];
        UIBackdropBlur m_backdropBlur;
        UICacheState m_cache;
        UIScrollBar m_scrollBar;
        int m_mouseX = 0;
        int m_mouseY = 0;
        int m_lastScreenW = 1;
        int m_lastScreenH = 1;
        int m_lastModelCapacity = SkullbonezCore::Scene::Capacity::DEFAULT_SCENE_OBJECT_CAPACITY;
        int m_lastSolverBallCount = 0;
        int m_lastSolverBoxCount = 0;
        int m_lastWorkerThreadCount = 0;
        int m_lastMaxWorkerThreadCount = 1;
        int m_lastRenderTargetPreviewCount = 0;
        uint32_t m_lastRenderTargetDisabledMask = 0;
        int m_selectedRenderTargetPreview = 0;
        bool m_hasMouseOverride = false;
        int m_mouseOverrideX = 0;
        int m_mouseOverrideY = 0;
        ControlsTab::UIControlsTabState m_controlsTab;
        EditorTab::UIEditorTabState m_editorTab;
        OptionsTab::UIOptionsTabState m_optionsTab;
        PhysicsTab::UIPhysicsTabState m_physicsTab;
        ProfilerTab::UIProfilerTabState m_profilerTab;
        MemoryTab::UIMemoryOverlayState m_memoryOverlay;
        SceneTab::UISceneTabState m_sceneTab;
        SkyTab::UISkyTabState m_skyTab;
        CinematicTab::UICinematicTabState m_cinematicTab;
        float m_scrollY = 0.0f;
        double m_scrollbarVisibleUntil = 0.0;
        int m_activeSlider = 0;
        bool m_hitboxOverlayEnabled = false;
        bool m_editorMiniPalettePressActive = false;
        bool m_editorMiniPaletteFlyoutOpen = false;
        int m_editorMiniPalettePressedEntry = -1;
        int m_editorMiniPalettePressedObjectType = -1;
        int m_editorMiniPalettePressedTreePlacement = -1;
        int m_editorMiniPalettePressedHoldMode = 0;
        double m_editorMiniPalettePressStart = 0.0;
    };

    } // namespace UI
} // namespace SkullbonezCore
