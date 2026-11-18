#pragma once

#include "PluginProcessor.h"
#include <cmath>
#include <algorithm>
#include <functional>

class VortexAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                       public juce::DragAndDropContainer,
                                       private juce::Timer,
                                       private juce::ListBoxModel
{
public:
    explicit VortexAudioProcessorEditor(VortexAudioProcessor&);
    ~VortexAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseEnter(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    using SliderAttachment =
        juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment =
        juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboBoxAttachment =
        juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    class DetentedSlider final : public juce::Slider
    {
    public:
        std::function<bool()> onSavedStateReset;
        void setCentreDetent(double value, double radius)
        {
            centreDetent = value;
            detentRadius = juce::jmax(0.0, radius);
            centreDetentEnabled = true;
            getProperties().set("hasCentreDetent", true);
            getProperties().set("centreDetentValue", value);
        }
        double snapValue(double attemptedValue, DragMode dragMode) override
        {
            if (centreDetentEnabled && dragMode != notDragging
                && std::abs(attemptedValue - centreDetent) <= detentRadius)
                return centreDetent;
            return attemptedValue;
        }
        void mouseDoubleClick(const juce::MouseEvent& event) override
        {
            if (onSavedStateReset != nullptr && onSavedStateReset())
                return;
            juce::Slider::mouseDoubleClick(event);
        }
    private:
        double centreDetent = 0.0;
        double detentRadius = 0.0;
        bool centreDetentEnabled = false;
    };

    class SteppedComboBox final : public juce::ComboBox
    {
    public:
        void mouseWheelMove(const juce::MouseEvent&,
                            const juce::MouseWheelDetails& wheel) override
        {
            if (! isEnabled() || getNumItems() <= 0
                || std::abs(wheel.deltaY) < 0.0001f)
                return;
            const int direction = wheel.deltaY > 0.0f ? -1 : 1;
            const auto now = juce::Time::getMillisecondCounter();
            if (direction == lastWheelDirection
                && now - lastWheelTimeMs < 100u)
                return;
            lastWheelDirection = direction;
            lastWheelTimeMs = now;
            const int current = juce::jmax(0, getSelectedItemIndex());
            const int next = juce::jlimit(
                0, getNumItems() - 1, current + direction);
            if (next != getSelectedItemIndex())
                setSelectedItemIndex(next, juce::sendNotificationSync);
        }
    private:
        juce::uint32 lastWheelTimeMs = 0;
        int lastWheelDirection = 0;
    };

    // The bottom contextual-help strip has room for exactly two lines.
    // Draw each line separately so a long description cannot wrap onto
    // a third line and get clipped vertically.
    class TwoLineHelpLabel final : public juce::Label
    {
    public:
        void paint(juce::Graphics&) override;
    };

    class PresetDisplayLabel final : public juce::Label
    {
    public:
        std::function<void(int)> onStep;
        std::function<void()> onShowMenu;
        void paint(juce::Graphics&) override;
        void mouseDown(const juce::MouseEvent&) override;
        void mouseWheelMove(const juce::MouseEvent&,
                            const juce::MouseWheelDetails&) override;
    private:
        juce::uint32 lastWheelTimeMs = 0;
        int lastWheelDirection = 0;
    };

    class PresetListBox final : public juce::ListBox
    {
    public:
        using juce::ListBox::ListBox;
        std::function<bool(int)> onHorizontalStep;
        bool keyPressed(const juce::KeyPress& key) override
        {
            int direction = 0;
            if (key.getKeyCode() == juce::KeyPress::leftKey)
                direction = -1;
            else if (key.getKeyCode() == juce::KeyPress::rightKey)
                direction = 1;
            if (direction != 0 && onHorizontalStep != nullptr
                && onHorizontalStep(direction))
                return true;
            return juce::ListBox::keyPressed(key);
        }
    };

    class ParameterDragLabel final : public juce::Label
    {
    public:
        std::function<juce::String()> parameterIdProvider;
        bool dragHandleOnly = false;
        bool dragHandleRight = false;
        void setTargetAppearance(bool assigned, juce::Colour accent);
        void setUnassignedColour(juce::Colour colour);
        void paint(juce::Graphics&) override;
        void mouseDrag(const juce::MouseEvent&) override;
    private:
        bool targetAssigned = false;
        juce::Colour targetAccent { 0xff34d6c6 };
        juce::Colour unassignedColour { 0xff8d949e };
    };

    struct Dial
    {
        DetentedSlider slider;
        ParameterDragLabel label;
        std::unique_ptr<SliderAttachment> attachment;
        juce::String help;
        juce::String parameterId;
    };

    class LogoHotspot final : public juce::Component,
                              public juce::SettableTooltipClient
    {
    public:
        std::function<void()> onClick;
        void mouseDown(const juce::MouseEvent&) override
        {
            if (onClick) onClick();
        }
    };

    class SequencerPanel;
    class FolderTreeItem;
    class LookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit LookAndFeel(bool compactPopupFont = false);
        juce::Label* createSliderTextBox(juce::Slider&) override;
        juce::Font getPopupMenuFont() override;
        juce::Font getComboBoxFont(juce::ComboBox&) override;
        void drawRotarySlider(juce::Graphics&, int, int, int, int,
                              float, float, float, juce::Slider&) override;
        void drawLinearSlider(juce::Graphics&, int, int, int, int,
                              float, float, float,
                              juce::Slider::SliderStyle,
                              juce::Slider&) override;
        void drawToggleButton(juce::Graphics&, juce::ToggleButton&,
                              bool, bool) override;
        void drawPopupMenuItem(juce::Graphics&,
                               const juce::Rectangle<int>&,
                               bool, bool, bool, bool, bool,
                               const juce::String&, const juce::String&,
                               const juce::Drawable*,
                               const juce::Colour*) override;
        void drawPopupMenuBackground(juce::Graphics&, int, int) override;
        juce::PopupMenu::Options getOptionsForComboBoxPopupMenu(
            juce::ComboBox&, juce::Label&) override;
        void setPopupBorderColour(juce::Colour colour)
        {
            popupBorderColour = colour;
        }
        juce::Colour getPopupBorderColour() const noexcept
        {
            return popupBorderColour;
        }
    private:
        juce::Colour popupBorderColour { 0xff1479b8 };
        bool useCompactPopupFont = false;
    };

    VortexAudioProcessor& processor;
    LookAndFeel lookAndFeel;
    LookAndFeel zoomMenuLookAndFeel;
    juce::Component content;
    std::vector<std::unique_ptr<Dial>> dials;
    std::array<Dial*, 26> effectDials {};
    std::array<Dial*, 2> phaserDetailDials {};
    std::array<Dial*, 4> effectLevelDials {};
    std::array<juce::ToggleButton, 4> effectEnabledButtons {};
    std::array<std::unique_ptr<ButtonAttachment>, 4> effectEnabledAttachments {};
    std::array<bool, 4> effectPanelEnabled { true, true, true, true };
    Dial* mixDial = nullptr;
    Dial* outputDial = nullptr;
    Dial* themeDial = nullptr;
    Dial* browserMixDial = nullptr;
    std::unique_ptr<juce::Drawable> logoDrawable;
    LogoHotspot logoHotspot;
    juce::Label versionLabel;
    juce::ToggleButton bypassButton;
    std::unique_ptr<juce::ParameterAttachment> bypassAttachment;
    juce::ToggleButton mixLockButton;
    juce::ToggleButton presetBrowserButton;
    juce::ToggleButton previousPresetButton;
    juce::ToggleButton nextPresetButton;
    PresetDisplayLabel currentPresetLabel;
    juce::TreeView presetFolderTree;
    PresetListBox presetList { "Preset list", this };
    juce::Label presetFolderLabel;
    juce::Label presetListLabel;
    juce::ToggleButton presetSaveButton;
    juce::ToggleButton presetSaveAsButton;
    juce::ToggleButton presetOpenFolderButton;
    juce::ToggleButton randomButton { "!!!" };
    juce::ToggleButton sequencerButton { "SEQ" };
    std::unique_ptr<SequencerPanel> sequencerPanel;
    std::unique_ptr<FolderTreeItem> presetFolderRoot;
    std::unique_ptr<juce::FileChooser> presetFileChooser;
    std::unique_ptr<juce::AlertWindow> presetFolderNameWindow;
    juce::Array<juce::File> visiblePresetFiles;
    juce::File selectedPresetFolder;
    juce::File currentPresetFile;
    juce::File copiedPresetFile;
    juce::StringArray favouritePresetPaths;
    TwoLineHelpLabel information;

    static constexpr int designWidth = 926;
    static constexpr int collapsedDesignHeight = 276;
    static constexpr int expandedDesignHeight = 530;
    int activeDesignHeight() const noexcept
    {
        return sequencerOpen ? expandedDesignHeight : collapsedDesignHeight;
    }

    Dial* addDial(const char*, const char*, const char*,
                  bool attachToParameter = true);
    bool restoreDialFromSavedState(Dial&);
    void updateContextSensitiveHelp();
    juce::Colour panelAccent(int filterIndex) const;
    juce::Colour panelColour(int filterIndex) const;
    void setPresetBrowserOpen(bool);
    void updatePresetBrowserVisibility();
    void rebuildPresetFolderTree();
    void selectPresetFolder(const juce::File&);
    void selectFavouritePresets();
    void refreshPresetList();
    void updatePresetNavigationDisplay();
    void setSequencerOpen(bool);
    void showZoomMenu();
    void applyZoom(int percentage);
    void updateZoomTooltip();
    void updateSequencerTargetHighlights();
    void updateEffectBypassAppearance();
    void updateAnimatedParameterDisplays();
    void randomiseEffectParameters();
    bool handlePresetCursorKey(int direction);
    void stepPreset(int direction);
    void showPresetQuickMenu();
    void loadPresetFile(const juce::File&);
    void loadPresetAtBrowserCell(int row, int column);
    void showPresetContextMenu(const juce::File&, juce::Point<int>);
    void showPresetFolderContextMenu(const juce::File&, bool,
                                     juce::Point<int>);
    void copyPreset(const juce::File&);
    void pastePresetIntoFolder(const juce::File&);
    void showCreatePresetFolderDialog(const juce::File&);
    void confirmDeletePresetFolder(const juce::File&);
    juce::Array<juce::File> favouritePresetFiles() const;
    juce::Array<juce::File> navigationPresetFiles() const;
    bool isPresetFavourite(const juce::File&) const;
    void togglePresetFavourite(const juce::File&);
    void loadFavouritePresets();
    void saveFavouritePresets() const;
    void saveCurrentPreset();
    void showPresetSaveAs();
    int getNumRows() override;
    void paintListBoxItem(int, juce::Graphics&, int, int, bool) override;
    void listBoxItemClicked(int, const juce::MouseEvent&) override;
    void returnKeyPressed(int) override;
    void selectedRowsChanged(int) override;
    void timerCallback() override;
    void saveInterfaceSettings() const;

    int observedEditorWidth = designWidth;
    double observedThemeValue = 100.0;
    int stableSettingsTicks = 0;
    bool settingsDirty = false;
    bool restoringLockedMix = false;
    double lockedMixValue = 35.0;
    bool presetBrowserOpen = false;
    bool favouritesViewSelected = false;
    bool sequencerOpen = false;
    bool previousPresetKeyDown = false;
    bool nextPresetKeyDown = false;
    int selectedPresetBrowserColumn = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VortexAudioProcessorEditor)
};
