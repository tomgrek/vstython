#pragma once

struct pycom {
    int note;
    float vol;
};

class Pyaudio {
public:
    Pyaudio() {
        seqnum = 0;
        audio = std::make_unique<MemoryBlock>(3840);
    }
    ~Pyaudio() {}
    int seqnum; // dont play if its same as last time
    std::unique_ptr<juce::MemoryBlock> audio;
    void reset() {
        audio->reset();
        seqnum = 0;
    }
    void set(juce::MemoryBlock newaudio, int seqnum_) {
        audio->swapWith(newaudio);
        seqnum = seqnum_;
    }
    juce::MemoryBlock* get() {
        return audio.get();
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
    }

    void connectionMade() override
    {
        DBG("Connection made\n");

        juce::String msg("EHLO");
        juce::MemoryBlock mb(msg.toRawUTF8(), msg.length());
        sendMessage(mb);
    }

    void connectionLost() override
    {
        DBG("Connection lost\n");
    }

    void messageReceived(const juce::MemoryBlock& msg) override
    {
        audiomsg[seqnum++ % BUF_SIZE].set(msg, seqnum);
        /*
        Here's the working code for MIDI
        midimsg = { true, 60, (float)0.8 };
        const auto str = msg.toString();
        DBG(str);
        auto note = str.substring(0, str.indexOf(0, "#"));
        auto vol = str.substring(str.indexOf(0, "#") + 1);
        midimsg.note = note.getIntValue();
        midimsg.vol = vol.getFloatValue();

        juce::String msg2("ACK");
        juce::MemoryBlock mb(msg2.toRawUTF8(), msg2.length());
        sendMessage(mb);
        if (str.contains("PANIC")) {
            midimsg.note = -1;
        }
        if (str.contains("STOP")) {
            stop_signal_.signal();
        }*/
    }
    Pyaudio* gotMsg(int theirSeqnum) {
        auto msg = &audiomsg[lastGot++ % BUF_SIZE];
        auto x = msg->get();
        tmp_audio.audio->replaceWith(x->getData(), x->getSize());
        tmp_audio.seqnum = msg->seqnum;
        msg->audio->fillWith(0);
        msg->seqnum = 0;
        return &tmp_audio;
    }
    void clearMsg() {
        audiomsg[seqnum].reset();
    }
    template <typename FloatType>
    void transmit(AudioBuffer<FloatType>& buffer) {
        int chunkSize = buffer.getNumSamples() * sizeof(float);
        if (!chunkSize) return;
        
        memory_block_ = std::make_unique<juce::MemoryBlock>(chunkSize * buffer.getNumChannels());
        //DBG(buffer.getNumSamples() << " chans " << buffer.getNumChannels() << " size " << sizeof(float));
        auto mb = memory_block_.get();

        mb->copyFrom((FloatType*)buffer.getReadPointer(0, 0), 0, chunkSize);
        mb->copyFrom((FloatType*)buffer.getReadPointer(1, 0), chunkSize, chunkSize);
        jassert(mb->getSize() == (chunkSize * buffer.getNumChannels()));
        sendMessage(*mb);
    }

private:
    juce::WaitableEvent& stop_signal_;
    std::unique_ptr<juce::MemoryBlock> memory_block_{ nullptr };
    Pyaudio tmp_audio;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Connection);
    int seqnum = 0;
    int lastGot = 0;
    int BUF_SIZE = 100;
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
        if (connection_) {
            connection_->disconnect();
        }
    }
    Pyaudio* gotMsg(int seqnum) {
        return connection_->gotMsg(seqnum);
    }
    bool isConnected() {
        if (!connection_) return false;
        return connection_->isConnected();
    }
    template <typename FloatType>
    void transmit(AudioBuffer<FloatType>& buffer) {
        connection_->transmit(buffer);
    }
private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(IPCServer);
protected:
    juce::InterprocessConnection* createConnectionObject() override
    {
        connection_ = std::make_unique<Connection>(stop_signal_);
        return connection_.get();
    }

    juce::WaitableEvent& stop_signal_;
    std::unique_ptr<Connection> connection_;
};

