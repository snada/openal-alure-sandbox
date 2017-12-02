#ifndef CONTEXT_H
#define CONTEXT_H

#include <condition_variable>
#include <unordered_map>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <stack>
#include <queue>
#include <set>

#include "main.h"

#include "device.h"
#include "source.h"


#define F_PI (3.14159265358979323846f)

namespace alure {

enum class AL {
    EXT_EFX,

    EXT_FLOAT32,
    EXT_MCFORMATS,
    EXT_BFORMAT,

    EXT_MULAW,
    EXT_MULAW_MCFORMATS,
    EXT_MULAW_BFORMAT,

    SOFT_loop_points,
    SOFT_source_latency,
    SOFT_source_resampler,
    SOFT_source_spatialize,

    EXT_disconnect,

    EXT_SOURCE_RADIUS,
    EXT_STEREO_ANGLES,

    EXTENSION_MAX
};

// Batches OpenAL updates while the object is alive, if batching isn't already
// in progress.
class Batcher {
    ALCcontext *mContext;

public:
    Batcher(ALCcontext *context) : mContext(context) { }
    Batcher(Batcher&& rhs) : mContext(rhs.mContext) { rhs.mContext = nullptr; }
    Batcher(const Batcher&) = delete;
    ~Batcher()
    {
        if(mContext)
            alcProcessContext(mContext);
    }

    Batcher& operator=(Batcher&&) = delete;
    Batcher& operator=(const Batcher&) = delete;
};


class ListenerImpl {
    ContextImpl *const mContext;

public:
    ListenerImpl(ContextImpl *ctx) : mContext(ctx) { }

    void setGain(ALfloat gain);

    void set3DParameters(const Vector3 &position, const Vector3 &velocity, const std::pair<Vector3,Vector3> &orientation);

    void setPosition(const Vector3 &position);
    void setPosition(const ALfloat *pos);

    void setVelocity(const Vector3 &velocity);
    void setVelocity(const ALfloat *vel);

    void setOrientation(const std::pair<Vector3,Vector3> &orientation);
    void setOrientation(const ALfloat *at, const ALfloat *up);
    void setOrientation(const ALfloat *ori);

    void setMetersPerUnit(ALfloat m_u);
};


using DecoderOrExceptT = std::variant<SharedPtr<Decoder>,std::exception_ptr>;
using BufferOrExceptT = std::variant<Buffer,std::exception_ptr>;

class ContextImpl {
    static ContextImpl *sCurrentCtx;
    static thread_local ContextImpl *sThreadCurrentCtx;

public:
    static void MakeCurrent(ContextImpl *context);
    static ContextImpl *GetCurrent()
    {
        auto thrd_ctx = sThreadCurrentCtx;
        return thrd_ctx ? thrd_ctx : sCurrentCtx;
    }

    static void MakeThreadCurrent(ContextImpl *context);
    static ContextImpl *GetThreadCurrent() { return sThreadCurrentCtx; }

    static std::atomic<uint64_t> sContextSetCount;
    mutable uint64_t mContextSetCounter{std::numeric_limits<uint64_t>::max()};

private:
    ListenerImpl mListener;
    ALCcontext *mContext{nullptr};
    Vector<ALuint> mSourceIds;

    struct PendingBuffer { BufferImpl *mBuffer;  SharedFuture<Buffer> mFuture; };
    struct PendingSource { SourceImpl *mSource;  SharedFuture<Buffer> mFuture; };

    DeviceImpl &mDevice;
    Vector<PendingBuffer> mFutureBuffers;
    Vector<UniquePtr<BufferImpl>> mBuffers;
    Vector<UniquePtr<SourceGroupImpl>> mSourceGroups;
    Vector<UniquePtr<AuxiliaryEffectSlotImpl>> mEffectSlots;
    Vector<UniquePtr<EffectImpl>> mEffects;
    std::deque<SourceImpl> mAllSources;
    Vector<SourceImpl*> mFreeSources;

    Vector<PendingSource> mPendingSources;
    Vector<SourceImpl*> mFadingSources;
    Vector<SourceBufferUpdateEntry> mPlaySources;
    Vector<SourceStreamUpdateEntry> mStreamSources;

    Vector<SourceImpl*> mStreamingSources;
    std::mutex mSourceStreamMutex;

    std::atomic<std::chrono::milliseconds> mWakeInterval{std::chrono::milliseconds::zero()};
    std::mutex mWakeMutex;
    std::condition_variable mWakeThread;

    SharedPtr<MessageHandler> mMessage;

    struct PendingPromise {
        BufferImpl *mBuffer;
        SharedPtr<Decoder> mDecoder;
        ALenum mFormat;
        ALuint mFrames;
        Promise<Buffer> mPromise;

        std::atomic<PendingPromise*> mNext;
    };
    std::atomic<PendingPromise*> mPendingCurrent{nullptr};
    PendingPromise *mPendingTail{nullptr};
    PendingPromise *mPendingHead{nullptr};

    std::atomic<bool> mQuitThread{false};
    std::thread mThread;
    void backgroundProc();

    size_t mRefs{0};

    Vector<String> mResamplers;

    Bitfield<static_cast<size_t>(AL::EXTENSION_MAX)> mHasExt;

    std::once_flag mSetExts;
    void setupExts();

    DecoderOrExceptT findDecoder(StringView name);
    BufferOrExceptT doCreateBuffer(StringView name, Vector<UniquePtr<BufferImpl>>::iterator iter, SharedPtr<Decoder> decoder);
    BufferOrExceptT doCreateBufferAsync(StringView name, Vector<UniquePtr<BufferImpl>>::iterator iter, SharedPtr<Decoder> decoder, Promise<Buffer> promise);

    bool mIsConnected : 1;
    bool mIsBatching : 1;

public:
    ContextImpl(DeviceImpl &device, ArrayView<AttributePair> attrs);
    ~ContextImpl();

    ALCcontext *getALCcontext() const { return mContext; }
    long addRef() { return ++mRefs; }
    long decRef() { return --mRefs; }

    bool hasExtension(AL ext) const { return mHasExt[static_cast<size_t>(ext)]; }

    LPALGETSTRINGISOFT alGetStringiSOFT{nullptr};
    LPALGETSOURCEI64VSOFT alGetSourcei64vSOFT{nullptr};
    LPALGETSOURCEDVSOFT alGetSourcedvSOFT{nullptr};

    LPALGENEFFECTS alGenEffects{nullptr};
    LPALDELETEEFFECTS alDeleteEffects{nullptr};
    LPALISEFFECT alIsEffect{nullptr};
    LPALEFFECTI alEffecti{nullptr};
    LPALEFFECTIV alEffectiv{nullptr};
    LPALEFFECTF alEffectf{nullptr};
    LPALEFFECTFV alEffectfv{nullptr};
    LPALGETEFFECTI alGetEffecti{nullptr};
    LPALGETEFFECTIV alGetEffectiv{nullptr};
    LPALGETEFFECTF alGetEffectf{nullptr};
    LPALGETEFFECTFV alGetEffectfv{nullptr};

    LPALGENFILTERS alGenFilters{nullptr};
    LPALDELETEFILTERS alDeleteFilters{nullptr};
    LPALISFILTER alIsFilter{nullptr};
    LPALFILTERI alFilteri{nullptr};
    LPALFILTERIV alFilteriv{nullptr};
    LPALFILTERF alFilterf{nullptr};
    LPALFILTERFV alFilterfv{nullptr};
    LPALGETFILTERI alGetFilteri{nullptr};
    LPALGETFILTERIV alGetFilteriv{nullptr};
    LPALGETFILTERF alGetFilterf{nullptr};
    LPALGETFILTERFV alGetFilterfv{nullptr};

    LPALGENAUXILIARYEFFECTSLOTS alGenAuxiliaryEffectSlots{nullptr};
    LPALDELETEAUXILIARYEFFECTSLOTS alDeleteAuxiliaryEffectSlots{nullptr};
    LPALISAUXILIARYEFFECTSLOT alIsAuxiliaryEffectSlot{nullptr};
    LPALAUXILIARYEFFECTSLOTI alAuxiliaryEffectSloti{nullptr};
    LPALAUXILIARYEFFECTSLOTIV alAuxiliaryEffectSlotiv{nullptr};
    LPALAUXILIARYEFFECTSLOTF alAuxiliaryEffectSlotf{nullptr};
    LPALAUXILIARYEFFECTSLOTFV alAuxiliaryEffectSlotfv{nullptr};
    LPALGETAUXILIARYEFFECTSLOTI alGetAuxiliaryEffectSloti{nullptr};
    LPALGETAUXILIARYEFFECTSLOTIV alGetAuxiliaryEffectSlotiv{nullptr};
    LPALGETAUXILIARYEFFECTSLOTF alGetAuxiliaryEffectSlotf{nullptr};
    LPALGETAUXILIARYEFFECTSLOTFV alGetAuxiliaryEffectSlotfv{nullptr};

    ALuint getSourceId(ALuint maxprio);
    void insertSourceId(ALuint id) { mSourceIds.push_back(id); }

    void addPendingSource(SourceImpl *source, SharedFuture<Buffer> future);
    void removePendingSource(SourceImpl *source);
    bool isPendingSource(const SourceImpl *source) const;
    void addFadingSource(SourceImpl *source);
    void removeFadingSource(SourceImpl *source);
    void addPlayingSource(SourceImpl *source, ALuint id);
    void addPlayingSource(SourceImpl *source);
    void removePlayingSource(SourceImpl *source);

    void addStream(SourceImpl *source);
    void removeStream(SourceImpl *source);
    void removeStreamNoLock(SourceImpl *source);

    void freeSource(SourceImpl *source) { mFreeSources.push_back(source); }
    void freeSourceGroup(SourceGroupImpl *group);
    void freeEffectSlot(AuxiliaryEffectSlotImpl *slot);
    void freeEffect(EffectImpl *effect);

    Batcher getBatcher()
    {
        if(mIsBatching)
            return Batcher(nullptr);
        alcSuspendContext(mContext);
        return Batcher(mContext);
    }

    std::unique_lock<std::mutex> getSourceStreamLock()
    { return std::unique_lock<std::mutex>(mSourceStreamMutex); }

    template<typename R, typename... Args>
    void send(R MessageHandler::* func, Args&&... args)
    { if(mMessage.get()) (mMessage.get()->*func)(std::forward<Args>(args)...); }

    Device getDevice() { return Device(&mDevice); }

    void destroy();

    void startBatch();
    void endBatch();

    Listener getListener() { return Listener(&mListener); }

    SharedPtr<MessageHandler> setMessageHandler(SharedPtr<MessageHandler>&& handler);
    SharedPtr<MessageHandler> getMessageHandler() const { return mMessage; }

    void setAsyncWakeInterval(std::chrono::milliseconds interval);
    std::chrono::milliseconds getAsyncWakeInterval() const { return mWakeInterval.load(); }

    SharedPtr<Decoder> createDecoder(StringView name);

    bool isSupported(ChannelConfig channels, SampleType type) const;

    ArrayView<String> getAvailableResamplers();
    ALsizei getDefaultResamplerIndex() const;

    Buffer getBuffer(StringView name);
    SharedFuture<Buffer> getBufferAsync(StringView name);
    void precacheBuffersAsync(ArrayView<StringView> names);
    Buffer createBufferFrom(StringView name, SharedPtr<Decoder>&& decoder);
    SharedFuture<Buffer> createBufferAsyncFrom(StringView name, SharedPtr<Decoder>&& decoder);
    Buffer findBuffer(StringView name);
    SharedFuture<Buffer> findBufferAsync(StringView name);
    void removeBuffer(StringView name);
    void removeBuffer(Buffer buffer) { removeBuffer(buffer.getName()); }

    Source createSource();

    AuxiliaryEffectSlot createAuxiliaryEffectSlot();

    Effect createEffect();

    SourceGroup createSourceGroup();

    void setDopplerFactor(ALfloat factor);

    void setSpeedOfSound(ALfloat speed);

    void setDistanceModel(DistanceModel model);

    void update();
};


inline void CheckContext(const ContextImpl &ctx)
{
    auto count = ContextImpl::sContextSetCount.load(std::memory_order_acquire);
    if(UNLIKELY(count != ctx.mContextSetCounter))
    {
        if(UNLIKELY(&ctx != ContextImpl::GetCurrent()))
            throw std::runtime_error("Called context is not current");
        ctx.mContextSetCounter = count;
    }
}

inline void CheckContexts(const ContextImpl &ctx0, const ContextImpl &ctx1)
{
    if(UNLIKELY(&ctx0 != &ctx1))
        throw std::runtime_error("Mismatched object contexts");
}


} // namespace alure

#endif /* CONTEXT_H */
