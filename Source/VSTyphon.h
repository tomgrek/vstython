/*******************************************************************************
 The block below describes the properties of this PIP. A PIP is a short snippet
 of code that can be read by the Projucer and used to generate a JUCE project.

 BEGIN_JUCE_PIP_METADATA

 name:                  VSTyphon
 version:               0.0.1
 vendor:                Tom Grek
 website:               https://tomgrek.com
 description:           Synthesiser audio plugin.

 dependencies:          juce_audio_basics, juce_audio_devices, juce_audio_formats,
                        juce_audio_plugin_client, juce_audio_processors,
                        juce_audio_utils, juce_core, juce_data_structures,
                        juce_events, juce_graphics, juce_gui_basics, juce_gui_extra
 exporters:             xcode_mac, vs2017, vs2019, linux_make, xcode_iphone, androidstudio

 moduleFlags:           JUCE_STRICT_REFCOUNTEDPOINTER=1

 type:                  AudioProcessor
 mainClass:             JuceDemoPluginAudioProcessor

 useLocalCopy:          1

 pluginCharacteristics: pluginIsSynth, pluginWantsMidiIn, pluginProducesMidiOut,
                        pluginEditorRequiresKeys
 extraPluginFormats:    AUv3

 END_JUCE_PIP_METADATA

*******************************************************************************/

#pragma once


//==============================================================================
/** A demo synth sound that's just a basic sine wave.. */
class SineWaveSound : public SynthesiserSound
{
public:
    SineWaveSound() {}

    bool appliesToNote (int /*midiNoteNumber*/) override    { return true; }
    bool appliesToChannel (int /*midiChannel*/) override    { return true; }
};

//==============================================================================
/** A simple demo synth voice that just plays a sine wave.. */
class SineWaveVoice   : public SynthesiserVoice
{
public:
    SineWaveVoice() {}

    bool canPlaySound (SynthesiserSound* sound) override
    {
        return dynamic_cast<SineWaveSound*> (sound) != nullptr;
    }

    void startNote (int midiNoteNumber, float velocity,
                    SynthesiserSound* /*sound*/,
                    int /*currentPitchWheelPosition*/) override
    {
        currentAngle = 0.0;
        level = velocity * 0.15;
        tailOff = 0.0;

        auto cyclesPerSecond = MidiMessage::getMidiNoteInHertz (midiNoteNumber);
        auto cyclesPerSample = cyclesPerSecond / getSampleRate();

        angleDelta = cyclesPerSample * MathConstants<double>::twoPi;
    }

    void stopNote (float /*velocity*/, bool allowTailOff) override
    {
        if (allowTailOff)
        {
            // start a tail-off by setting this flag. The render callback will pick up on
            // this and do a fade out, calling clearCurrentNote() when it's finished.

            if (tailOff == 0.0) // we only need to begin a tail-off if it's not already doing so - the
                                // stopNote method could be called more than once.
                tailOff = 1.0;
        }
        else
        {
            // we're being told to stop playing immediately, so reset everything..

            clearCurrentNote();
            angleDelta = 0.0;
        }
    }

    void pitchWheelMoved (int /*newValue*/) override
    {
        // not implemented for the purposes of this demo!
    }

    void controllerMoved (int /*controllerNumber*/, int /*newValue*/) override
    {
        // not implemented for the purposes of this demo!
    }

    void renderNextBlock (AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override
    {
        if (angleDelta != 0.0)
        {
            if (tailOff > 0.0)
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float) (sin (currentAngle) * level * tailOff);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;

                    tailOff *= 0.99;

                    if (tailOff <= 0.005)
                    {
                        // tells the synth that this voice has stopped
                        clearCurrentNote();

                        angleDelta = 0.0;
                        break;
                    }
                }
            }
            else
            {
                while (--numSamples >= 0)
                {
                    auto currentSample = (float) (sin (currentAngle) * level);

                    for (auto i = outputBuffer.getNumChannels(); --i >= 0;)
                        outputBuffer.addSample (i, startSample, currentSample);

                    currentAngle += angleDelta;
                    ++startSample;
                }
            }
        }
    }

    using SynthesiserVoice::renderNextBlock;

private:
    double currentAngle = 0.0;
    double angleDelta   = 0.0;
    double level        = 0.0;
    double tailOff      = 0.0;
};










class JuceDemoPluginAudioProcessor  : public AudioProcessor
{
public:
    
    class TomThreader : public juce::Thread {
    public:
        TomThreader() : Thread("wot") {
            server = std::make_unique<IPCServer>(stop_signal);
        }
        ~TomThreader() {
            server = nullptr;
        }
        juce::WaitableEvent stop_signal;
        std::unique_ptr<IPCServer> server;

        void run() override
        {


            while (!juce::Thread::threadShouldExit())
            {
                if (server->beginWaitingForSocket(11586)) {
                    while (!stop_signal.wait(1000)) {
                        // safe to do nothing here but have to reconnect python side
                    }
                    DBG("madeanoops");
                }
            }
            DBG("FINISHING HERE");
        }

        juce::MidiMessage getMidi() {
            MidiMessage midi = MidiMessage::noteOff(1, last_note);
            /*pyaudio zig = server->gotMsg();
            if (zig.did_get) {
                last_note = zig.note;
                midi = MidiMessage::noteOn(1, zig.note, zig.vol);
            }*/

            return midi;
        }

        Pyaudio* getAudio(int seqnum) {
            return server->gotMsg(seqnum);
        }

        bool getPanic() {
            return last_note == -1;
        }
        template <typename FloatType>
        void transmit(AudioBuffer<FloatType>& buffer) {
            if (server->isConnected()) {
                server->transmit(buffer);
            }
        }
        bool isConnected() {
            return server->isConnected();
        }

    private:
        int last_note = 0;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TomThreader);
    };


    JuceDemoPluginAudioProcessor()
        : AudioProcessor (getBusesProperties()),
          state (*this, nullptr, "state",
                 { std::make_unique<AudioParameterFloat> ("gain",  "Gain",           NormalisableRange<float> (0.0f, 1.0f), 0.9f),
                   std::make_unique<AudioParameterFloat> ("delay", "Delay Feedback", NormalisableRange<float> (0.0f, 1.0f), 0.5f) })
    {
        // Add a sub-tree to store the state of our UI
        state.state.addChild ({ "uiState", { { "width",  400 }, { "height", 200 } }, {} }, -1, nullptr);

        initialiseSynth();

        tomThread.startThread();
        
        

    }
    ~JuceDemoPluginAudioProcessor() override {
        DBG("killingithere");
        tomThread.stop_signal.signal();
        tomThread.stopThread(1000);
    }

    //==============================================================================
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        // Only mono/stereo and input/output must have same layout
        const auto& mainOutput = layouts.getMainOutputChannelSet();
        const auto& mainInput  = layouts.getMainInputChannelSet();

        // input and output layout must either be the same or the input must be disabled altogether
        if (! mainInput.isDisabled() && mainInput != mainOutput)
            return false;

        // do not allow disabling the main buses
        if (mainOutput.isDisabled())
            return false;

        // only allow stereo and mono
        if (mainOutput.size() > 2)
            return false;

        return true;
    }

    void prepareToPlay (double newSampleRate, int samplesPerBlock) override
    {
        // Use this method as the place to do any pre-playback
        // initialisation that you need..
        synth.setCurrentPlaybackSampleRate (newSampleRate);
        keyboardState.reset();

        if (isUsingDoublePrecision())
        {
            delayBufferDouble.setSize (2, 12000);
            delayBufferFloat .setSize (1, 1);
        }
        else
        {
            delayBufferFloat .setSize (2, 12000);
            delayBufferDouble.setSize (1, 1);
        }

        reset();
    }

    void releaseResources() override
    {
        // When playback stops, you can use this as an opportunity to free up any
        // spare memory, etc.
        keyboardState.reset();
    }

    void reset() override
    {
        // Use this method as the place to clear any delay lines, buffers, etc, as it
        // means there's been a break in the audio's continuity.
        DBG("clearnig delay buf");
        delayBufferFloat .clear();
        delayBufferDouble.clear();
    }

    //==============================================================================
    void processBlock (AudioBuffer<float>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (! isUsingDoublePrecision());
        process (buffer, midiMessages, delayBufferFloat);
    }

    void processBlock (AudioBuffer<double>& buffer, MidiBuffer& midiMessages) override
    {
        jassert (isUsingDoublePrecision());
        process (buffer, midiMessages, delayBufferDouble);
    }

    //==============================================================================
    bool hasEditor() const override                                   { return true; }

    AudioProcessorEditor* createEditor() override
    {
        return new JuceDemoPluginAudioProcessorEditor (*this);
    }

    //==============================================================================
    const String getName() const override                             { return "VSTyphon"; }
    bool acceptsMidi() const override                                 { return true; }
    bool producesMidi() const override                                { return true; }
    double getTailLengthSeconds() const override                      { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                                     { return 0; }
    int getCurrentProgram() override                                  { return 0; }
    void setCurrentProgram (int) override                             {}
    const String getProgramName (int) override                        { return {}; }
    void changeProgramName (int, const String&) override              {}

    TomThreader tomThread;

    //==============================================================================
    void getStateInformation (MemoryBlock& destData) override
    {
        // Store an xml representation of our state.
        if (auto xmlState = state.copyState().createXml())
            copyXmlToBinary (*xmlState, destData);
    }

    void setStateInformation (const void* data, int sizeInBytes) override
    {
        // Restore our plug-in's state from the xml representation stored in the above
        // method.
        if (auto xmlState = getXmlFromBinary (data, sizeInBytes))
            state.replaceState (ValueTree::fromXml (*xmlState));
    }

    //==============================================================================
    void updateTrackProperties (const TrackProperties& properties) override
    {
        {
            const ScopedLock sl (trackPropertiesLock);
            trackProperties = properties;
        }

        MessageManager::callAsync ([this]
        {
            if (auto* editor = dynamic_cast<JuceDemoPluginAudioProcessorEditor*> (getActiveEditor()))
                 editor->updateTrackProperties();
        });
    }

    TrackProperties getTrackProperties() const
    {
        const ScopedLock sl (trackPropertiesLock);
        return trackProperties;
    }

    class SpinLockedPosInfo
    {
    public:
        SpinLockedPosInfo() { info.resetToDefault(); }

        // Wait-free, but setting new info may fail if the main thread is currently
        // calling `get`. This is unlikely to matter in practice because
        // we'll be calling `set` much more frequently than `get`.
        void set (const AudioPlayHead::CurrentPositionInfo& newInfo)
        {
            const juce::SpinLock::ScopedTryLockType lock (mutex);

            if (lock.isLocked())
                info = newInfo;
        }

        AudioPlayHead::CurrentPositionInfo get() const noexcept
        {
            const juce::SpinLock::ScopedLockType lock (mutex);
            return info;
        }

    private:
        juce::SpinLock mutex;
        AudioPlayHead::CurrentPositionInfo info;
    };

    MidiKeyboardState keyboardState;

    SpinLockedPosInfo lastPosInfo;

    // Our plug-in's current state
    AudioProcessorValueTreeState state;

private:
    //==============================================================================
    /** This is the editor component that our filter will display. */
    class JuceDemoPluginAudioProcessorEditor  : public AudioProcessorEditor,
                                                private Timer,
                                                private Value::Listener
    {
    public:
        JuceDemoPluginAudioProcessorEditor(JuceDemoPluginAudioProcessor& owner)
            : AudioProcessorEditor(owner),
              midiKeyboard         (owner.keyboardState, MidiKeyboardComponent::horizontalKeyboard),
              gainAttachment       (owner.state, "gain",  gainSlider),
              delayAttachment      (owner.state, "delay", delaySlider)

        {
            // add some sliders..
            addAndMakeVisible (gainSlider);
            gainSlider.setSliderStyle (Slider::Rotary);

            addAndMakeVisible (delaySlider);
            delaySlider.setSliderStyle (Slider::Rotary);

            // add some labels for the sliders..
            gainLabel.attachToComponent (&gainSlider, false);
            gainLabel.setFont (Font (11.0f));

            delayLabel.attachToComponent (&delaySlider, false);
            delayLabel.setFont (Font (11.0f));

            // add the midi keyboard component..
            addAndMakeVisible (midiKeyboard);

            // add a label that will display the current timecode and status..
            addAndMakeVisible (timecodeDisplayLabel);
            timecodeDisplayLabel.setFont (Font (Font::getDefaultMonospacedFontName(), 15.0f, Font::plain));

            // set resize limits for this plug-in
            setResizeLimits (400, 200, 1024, 700);
            setResizable (true, owner.wrapperType != wrapperType_AudioUnitv3);

            lastUIWidth .referTo (owner.state.state.getChildWithName ("uiState").getPropertyAsValue ("width",  nullptr));
            lastUIHeight.referTo (owner.state.state.getChildWithName ("uiState").getPropertyAsValue ("height", nullptr));

            // set our component's initial size to be the last one that was stored in the filter's settings
            setSize (lastUIWidth.getValue(), lastUIHeight.getValue());

            lastUIWidth. addListener (this);
            lastUIHeight.addListener (this);

            updateTrackProperties();

            // start a timer which will keep our timecode display updated
            startTimerHz (30);
        }

        ~JuceDemoPluginAudioProcessorEditor() override {}

        //==============================================================================
        void paint (Graphics& g) override
        {
            g.setColour (backgroundColour);
            g.fillAll();
        }

        void resized() override
        {
            // This lays out our child components...

            auto r = getLocalBounds().reduced (8);

            timecodeDisplayLabel.setBounds (r.removeFromTop (26));
            midiKeyboard        .setBounds (r.removeFromBottom (70));

            r.removeFromTop (20);
            auto sliderArea = r.removeFromTop (60);
            gainSlider.setBounds  (sliderArea.removeFromLeft (jmin (180, sliderArea.getWidth() / 2)));
            delaySlider.setBounds (sliderArea.removeFromLeft (jmin (180, sliderArea.getWidth())));

            lastUIWidth  = getWidth();
            lastUIHeight = getHeight();
        }

        void timerCallback() override
        {
            updateTimecodeDisplay (getProcessor().lastPosInfo.get());
        }

        void hostMIDIControllerIsAvailable (bool controllerIsAvailable) override
        {
            midiKeyboard.setVisible (! controllerIsAvailable);
        }

        int getControlParameterIndex (Component& control) override
        {
            if (&control == &gainSlider)
                return 0;

if (&control == &delaySlider)
return 1;

return -1;
        }

        void updateTrackProperties()
        {
            auto trackColour = getProcessor().getTrackProperties().colour;
            auto& lf = getLookAndFeel();

            backgroundColour = (trackColour == Colour() ? lf.findColour(ResizableWindow::backgroundColourId)
                : trackColour.withAlpha(1.0f).withBrightness(0.266f));
            repaint();
        }

    private:
        MidiKeyboardComponent midiKeyboard;
        Label timecodeDisplayLabel,
            gainLabel{ {}, "Throughput level:" },
            delayLabel{ {}, "Delay:" };

        Slider gainSlider, delaySlider;
        AudioProcessorValueTreeState::SliderAttachment gainAttachment, delayAttachment;
        Colour backgroundColour;
        Value lastUIWidth, lastUIHeight;

        JuceDemoPluginAudioProcessor& getProcessor() const
        {
            return static_cast<JuceDemoPluginAudioProcessor&> (processor);
        }

        // quick-and-dirty function to format a timecode string
        static String timeToTimecodeString(double seconds)
        {
            auto millisecs = roundToInt(seconds * 1000.0);
            auto absMillisecs = std::abs(millisecs);

            return String::formatted("%02d:%02d:%02d.%03d",
                millisecs / 3600000,
                (absMillisecs / 60000) % 60,
                (absMillisecs / 1000) % 60,
                absMillisecs % 1000);
        }

        // quick-and-dirty function to format a bars/beats string
        static String quarterNotePositionToBarsBeatsString(double quarterNotes, int numerator, int denominator)
        {
            if (numerator == 0 || denominator == 0)
                return "1|1|000";

            auto quarterNotesPerBar = (numerator * 4 / denominator);
            auto beats = (fmod(quarterNotes, quarterNotesPerBar) / quarterNotesPerBar) * numerator;

            auto bar = ((int)quarterNotes) / quarterNotesPerBar + 1;
            auto beat = ((int)beats) + 1;
            auto ticks = ((int)(fmod(beats, 1.0) * 960.0 + 0.5));

            return String::formatted("%d|%d|%03d", bar, beat, ticks);
        }

        
        void updateTimecodeDisplay(AudioPlayHead::CurrentPositionInfo pos)
        {
            MemoryOutputStream displayText;

            //displayText 
            //    << String(pos.bpm, 2) << " bpm, "
            //    << pos.timeSigNumerator << '/' << pos.timeSigDenominator
            //    << "  -  " << timeToTimecodeString(pos.timeInSeconds)
            //    << "  -  " << quarterNotePositionToBarsBeatsString(pos.ppqPosition,
            //        pos.timeSigNumerator,
            //        pos.timeSigDenominator);

            //if (pos.isRecording)
            //    displayText << "  (recording)";
            //else if (pos.isPlaying)
            //    displayText << "  (playing)";
    
            if (getProcessor().tomThread.isConnected()) {
                displayText << "Connected";
            } else {
                displayText << "Connect to localhost:11586";
            }
            timecodeDisplayLabel.setText(displayText.toString(), dontSendNotification);
        }

        // called when the stored window size changes
        void valueChanged(Value&) override
        {
            setSize(lastUIWidth.getValue(), lastUIHeight.getValue());
        }
    };

    template <typename FloatType>
    void process(AudioBuffer<FloatType>& buffer, MidiBuffer& midiMessages, AudioBuffer<FloatType>& delayBuffer)
    {
        auto gainParamValue = state.getParameter("gain")->getValue();
        auto delayParamValue = state.getParameter("delay")->getValue();
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();
        keyboardState.processNextMidiBuffer(midiMessages, 0, numSamples, true);

        synth.renderNextBlock(buffer, midiMessages, 0, numSamples);

        if (tomThread.isConnected()) {
            tomThread.transmit(buffer);
            Pyaudio* audio = tomThread.getAudio(seqnum);
                
            //buffer.setDataToReferTo((FloatType **)audio->get()->getData(), 2, 480);
            // ^^ that MIGHT work, later on

            float* memory_block = reinterpret_cast<float*>(audio->get()->getData());
            auto **data = buffer.getArrayOfWritePointers();
            for (int ch = 0; ch < numChannels; ch++) {
                for (auto i = 0; i < numSamples; i++) {
                    data[ch][i] = (float)memory_block[i + (ch * numSamples)];
                }
            }
            //if (audio->seqnum == 500) {
            //    for (int ch = 0; ch < numChannels; ch++) {
            //        for (auto i = 0; i < numSamples; i++) {
            //            DBG("at " << i << " data " << data[ch][i]);
            //        }
            //    }
            //    DBG("is500");
            //}
            //DBG(buffer.getRMSLevel(0, 0, numSamples));
            //seqnum = audio->seqnum;
        }
        seqnum++;
        //DBG("C++ seqnum " << seqnum << " recvd seqnum " << audio->seqnum);
        //Time::waitForMillisecondCounter(Time::getMillisecondCounter() + 100);
        delayBuffer.clear();
        //applyGain(buffer, delayBuffer, gainParamValue);

        //MidiBuffer& newMessages = MidiBuffer(midi);

        
        //applyDelay (buffer, delayBuffer, delayParamValue);

        //updateCurrentTimeInfoFromHost();

    }

    template <typename FloatType>
    void applyGain (AudioBuffer<FloatType>& buffer, AudioBuffer<FloatType>& delayBuffer, float gainLevel)
    {
        ignoreUnused (delayBuffer);

        for (auto channel = 0; channel < getTotalNumOutputChannels(); ++channel)
            buffer.applyGain (channel, 0, buffer.getNumSamples(), gainLevel);
    }

    template <typename FloatType>
    void applyDelay (AudioBuffer<FloatType>& buffer, AudioBuffer<FloatType>& delayBuffer, float delayLevel)
    {
        auto numSamples = buffer.getNumSamples();

        auto delayPos = 0;

        for (auto channel = 0; channel < getTotalNumOutputChannels(); ++channel)
        {
            auto channelData = buffer.getWritePointer (channel);
            auto delayData = delayBuffer.getWritePointer (jmin (channel, delayBuffer.getNumChannels() - 1));
            delayPos = delayPosition;

            for (auto i = 0; i < numSamples; ++i)
            {
                auto in = channelData[i];
                channelData[i] += delayData[delayPos];
                delayData[delayPos] = (delayData[delayPos] + in) * delayLevel;

                if (++delayPos >= delayBuffer.getNumSamples())
                    delayPos = 0;
            }
        }

        delayPosition = delayPos;
    }

    AudioBuffer<float> delayBufferFloat;
    AudioBuffer<double> delayBufferDouble;

    int delayPosition = 0;
    int seqnum = 0;

    Synthesiser synth;

    CriticalSection trackPropertiesLock;
    TrackProperties trackProperties;

    void initialiseSynth()
    {
        auto numVoices = 8;

        // Add some voices...
        for (auto i = 0; i < numVoices; ++i)
            synth.addVoice (new SineWaveVoice());

        // ..and give the synth a sound to play
        synth.addSound (new SineWaveSound());
    }

    void updateCurrentTimeInfoFromHost()
    {
        const auto newInfo = [&]
        {
            if (auto* ph = getPlayHead())
            {
                AudioPlayHead::CurrentPositionInfo result;

                if (ph->getCurrentPosition (result))
                    return result;
            }

            // If the host fails to provide the current time, we'll just use default values
            AudioPlayHead::CurrentPositionInfo result;
            result.resetToDefault();
            return result;
        }();

        lastPosInfo.set (newInfo);
    }

    static BusesProperties getBusesProperties()
    {
        return BusesProperties().withInput  ("Input",  AudioChannelSet::stereo(), true)
                                .withOutput ("Output", AudioChannelSet::stereo(), true);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JuceDemoPluginAudioProcessor)
};