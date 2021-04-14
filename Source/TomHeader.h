#pragma once

typedef struct pycom {
    bool did_get;
    int note;
    float vol;
};

class Pyaudio {
public:
    Pyaudio() {
        did_get = false;
        seqnum = 0;
        audio = std::make_unique<MemoryBlock>();
    }
    ~Pyaudio() {}
    bool did_get;
    int seqnum; // dont play if its same as last time
    std::unique_ptr<juce::MemoryBlock> audio;
    bool written = true;
    void reset() {
        audio->reset();
        did_get = false;
        seqnum = 0;
    }
    void set(juce::MemoryBlock newaudio) {
        audio->swapWith(newaudio);
        did_get = true;
        seqnum++;
        if (seqnum == 500) {
            DBG("is 500");
        }
    }
    juce::MemoryBlock* get() {
        return audio.get();
    }
};

class Connection : public juce::InterprocessConnection, juce::ActionBroadcaster, juce::ReferenceCountedObject
{
public:
    Connection(juce::WaitableEvent& stop_signal)
        : InterprocessConnection(false, 15),
        stop_signal_(stop_signal)
    {
        using Ptr = ReferenceCountedObjectPtr<Connection>;
        
        audiomsg = std::make_unique<Pyaudio>();
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
        audiomsg->reset();
        //disconnect();
    }

    void messageReceived(const juce::MemoryBlock& msg) override
    {
        audiomsg->set(msg);
        // 
        // 
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
    Pyaudio* gotMsg() {
        if (this == nullptr) return nullptr; // return new Pyaudio();

        return audiomsg.get();
    }
    void clearMsg() {
        audiomsg->reset();
    }
    template <typename FloatType>
    void transmit(AudioBuffer<FloatType>& buffer) {
        
        if (!buffer.getNumSamples()) return;
        memory_block_ = std::make_unique<juce::MemoryBlock>(buffer.getNumSamples() * buffer.getNumChannels() * sizeof(double));
        memory_block_.get()->copyFrom(buffer.getReadPointer(0, 0), 0, buffer.getNumSamples() * sizeof(double));
        memory_block_.get()->append(buffer.getReadPointer(1, 0), buffer.getNumSamples() * sizeof(double));

        memory_block_->ensureSize(buffer.getNumSamples() * sizeof(double) * 2, true);
        sendMessage(*memory_block_.get());
        //audiomsg->set(*memory_block_.get());// <------------------------the key to doing it locally.

    }

private:
    juce::WaitableEvent& stop_signal_;
    std::unique_ptr<juce::MemoryBlock> memory_block_{ nullptr };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Connection);
protected:
    std::unique_ptr<Pyaudio> audiomsg;
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
    Pyaudio* gotMsg() {
        Pyaudio* msg = connection_->gotMsg();
        //if (msg->did_get) connection_->clearMsg();
        return msg;
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

