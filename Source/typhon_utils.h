#pragma once

struct pycom {
    int note;
    float vol;
};

class Pyaudio {
public:
    Pyaudio() {
        seqnum = 0;
        audio = std::make_unique<MemoryBlock>(12000);
        midi = std::make_unique<MemoryBlock>(300 * sizeof(uint8));
    }
    ~Pyaudio() {}
    int seqnum; // dont play if its same as last time
    std::unique_ptr<juce::MemoryBlock> audio;
    std::unique_ptr<juce::MemoryBlock> midi;
    void reset() {
        audio->reset();
        midi->reset();
        seqnum = 0;
    }
    void set(juce::MemoryBlock newaudio, juce::MemoryBlock newmidi, int seqnum_) {
        audio->swapWith(newaudio);
        midi->swapWith(newmidi);
        seqnum = seqnum_;
    }
    juce::MemoryBlock* getAudio() {
        return audio.get();
    }
    juce::MemoryBlock* getMidi() {
        return midi.get();
    }
};

class Connection : public juce::InterprocessConnection, juce::ActionBroadcaster, juce::ReferenceCountedObject
{
public:
    Connection(juce::WaitableEvent& stop_signal)
        : InterprocessConnection(true, 15),
        stop_signal_(stop_signal)
    {
        audiomsg = std::make_unique<Pyaudio[]>(BUF_SIZE);
        memory_block_ = std::make_unique<juce::MemoryBlock>();
        tmp2 = std::make_unique<juce::MemoryBlock>(12000);
        tmpMidi2 = std::make_unique<juce::MemoryBlock>(300 * sizeof(uint8));
    }

    void connectionMade() override
    {
        DBG("Connection made");

        juce::String msg("EHLO" + timecodeInfo + "BYE");
        juce::MemoryBlock mb(msg.toRawUTF8(), msg.length());
        sendMessage(mb);
    }

    void connectionLost() override
    {
        disconnect(10, Notify::yes);
    }

    void saveTimecodeInfo(std::string info)
    {
        timecodeInfo = info;
    }

    void messageReceived(const juce::MemoryBlock& msg) override
    {
        if (seqnum == 0) {
            seqnum = 0;
            lastGot = 0;
        }
        else if (seqnum > lastGot) {
            lastGot = seqnum - 1;
        }

        int16* msg_data = (int16*)msg.getData();
        uint8* msg_data_midi = (uint8*)msg.getData();
        float* tmp = (float*)tmp2->getData();
        uint8* tmpMidi = (uint8*)tmpMidi2->getData();
        int msg_size = msg.getSize();
        int samples_by_channels = (msg_size - (300 * sizeof(uint8))) / 2;
        tmp2->setSize((samples_by_channels * sizeof(float)) );
        tmpMidi2->setSize(300);
        for (int i = 0; i < samples_by_channels; i++) {
            tmp[i] = (float)(msg_data[i] / 32768.0f);
        }
        msg.copyTo(tmpMidi, msg_size - 300, 300);
        audiomsg[seqnum++ % BUF_SIZE].set(*tmp2.get(), *tmpMidi2.get(), seqnum);
    }
    Pyaudio* gotMsg() {
        if (seqnum > lastGot) {
            lastGot = seqnum - 1;
        }
        auto msg = &audiomsg[lastGot++ % BUF_SIZE];
        auto x = msg->getAudio();
        auto x_midi = msg->getMidi();
        tmp_audio.audio->replaceWith(x->getData(), x->getSize());
        tmp_audio.midi->replaceWith(x_midi->getData(), x_midi->getSize());
        tmp_audio.seqnum = msg->seqnum - 1;
        msg->audio->fillWith(0);
        msg->midi->fillWith(0);
        msg->seqnum = 0;
        return &tmp_audio;
    }
    void clearMsg() {
        audiomsg[seqnum].reset();
    }
    template <typename FloatType>
    void transmit(AudioBuffer<FloatType>& buffer, MidiBuffer & midiBuffer) {
        int chunkSize = buffer.getNumSamples() * sizeof(float);
        if (!chunkSize) return;
        int totalSamples = buffer.getNumSamples() * buffer.getNumChannels();

        auto midi = midiBuffer.data.begin();
        auto midiSize = midiBuffer.data.size();
        midi_block_ = std::make_unique<juce::MemoryBlock>(midiSize * sizeof(uint8));
        auto midib = midi_block_.get();
        midib->copyFrom(midi, 0, midiSize);
        midib->ensureSize(300, true); // 300 (/3 bytes/event) events per 10ms is a lot...
        
        memory_block_ = std::make_unique<juce::MemoryBlock>(chunkSize * buffer.getNumChannels());
        auto mb = memory_block_.get();

        mb->copyFrom((FloatType*)buffer.getReadPointer(0, 0), 0, chunkSize);
        mb->copyFrom((FloatType*)buffer.getReadPointer(1, 0), chunkSize, chunkSize);
        jassert(mb->getSize() == (chunkSize * buffer.getNumChannels()));

        MemoryBlock toSend((int)((mb->getSize() / 2) + midib->getSize()));
        int16* p = (int16*)toSend.getData();
        float* channelData = (float*)mb->getData();
        uint8* midiData = (uint8*)midib->getData();
        for (int s = 0; s < totalSamples; ++s) {
            p[s] = (int16)(channelData[s] * 32768.0f);
            // TODO add max(1) and min(-1) if clipped
        }
        uint8* p2 = (uint8*)toSend.getData();
        for (int s = totalSamples * sizeof(int16); s < (totalSamples * sizeof(int16)) + (300 * sizeof(uint8)); s++) {
            p2[s] = (uint8)(midiData[s - (totalSamples * sizeof(int16))]);
        }
        sendMessage(toSend);
    }

private:
    juce::WaitableEvent& stop_signal_;
    std::unique_ptr<juce::MemoryBlock> memory_block_{ nullptr };
    std::unique_ptr<juce::MemoryBlock> midi_block_{ nullptr };
    std::unique_ptr<juce::MemoryBlock> tmp2{ nullptr };
    std::unique_ptr<juce::MemoryBlock> tmpMidi2{ nullptr };
    Pyaudio tmp_audio;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Connection);
    int seqnum = 0;
    int lastGot = 0;
    int BUF_SIZE = 100;
    std::string timecodeInfo = "";
protected:
    std::unique_ptr<Pyaudio[]> audiomsg;
};

class IPCServer : public juce::InterprocessConnectionServer
{
public:
    IPCServer(juce::WaitableEvent& stop_signal)
        : stop_signal_(stop_signal), connection_(nullptr)
    {
    }

    ~IPCServer()
    {
        if (isConnected()) {
            connection_->disconnect();
        }
    }
    Pyaudio* gotMsg() {
        return connection_->gotMsg();
    }
    bool isConnected() {
        if (!connection_) return false;
        return connection_->isConnected();
    }
    template <typename FloatType>
    void transmit(AudioBuffer<FloatType>& buffer, MidiBuffer& midiBuffer) {
        if (isConnected()) {
            connection_->transmit(buffer, midiBuffer);
        }
    }
    void saveTimecodeInfo(std::string info_) {
        info = info_;
    }
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IPCServer);
protected:
    juce::InterprocessConnection* createConnectionObject() override
    {
        if (connection_) {
            connection_->disconnect();
        }
        connection_ = std::make_unique<Connection>(stop_signal_);
        auto conn = connection_.get();
        conn->saveTimecodeInfo(info);
        return conn;
    }

    juce::WaitableEvent& stop_signal_;
    std::unique_ptr<Connection> connection_;
    std::string info;
};

