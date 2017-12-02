
#include "config.h"

#include "source.h"

#include <cstring>

#include <stdexcept>
#include <memory>
#include <limits>

#include "context.h"
#include "buffer.h"
#include "auxeffectslot.h"
#include "sourcegroup.h"

namespace alure
{

class ALBufferStream {
    SharedPtr<Decoder> mDecoder;

    ALuint mUpdateLen{0};
    ALuint mNumUpdates{0};

    ALenum mFormat{AL_NONE};
    ALuint mFrequency{0};
    ALuint mFrameSize{0};

    Vector<ALbyte> mData;
    ALbyte mSilence{0};

    Vector<ALuint> mBufferIds;
    ALuint mCurrentIdx{0};

    uint64_t mSamplePos{0};
    std::pair<uint64_t,uint64_t> mLoopPts{0,0};
    bool mHasLooped{false};
    std::atomic<bool> mDone{false};

public:
    ALBufferStream(SharedPtr<Decoder> decoder, ALuint updatelen, ALuint numupdates)
      : mDecoder(decoder), mUpdateLen(updatelen), mNumUpdates(numupdates)
    { }
    ~ALBufferStream()
    {
        if(!mBufferIds.empty())
        {
            alDeleteBuffers(mBufferIds.size(), mBufferIds.data());
            mBufferIds.clear();
        }
    }

    uint64_t getPosition() const { return mSamplePos; }

    ALuint getNumUpdates() const { return mNumUpdates; }
    ALuint getUpdateLength() const { return mUpdateLen; }

    ALuint getFrequency() const { return mFrequency; }

    bool seek(uint64_t pos)
    {
        if(!mDecoder->seek(pos))
            return false;
        mSamplePos = pos;
        mHasLooped = false;
        mDone.store(false, std::memory_order_release);
        return true;
    }

    void prepare()
    {
        ALuint srate = mDecoder->getFrequency();
        ChannelConfig chans = mDecoder->getChannelConfig();
        SampleType type = mDecoder->getSampleType();

        mLoopPts = mDecoder->getLoopPoints();
        if(mLoopPts.first >= mLoopPts.second)
        {
            mLoopPts.first = 0;
            mLoopPts.second = std::numeric_limits<uint64_t>::max();
        }

        mFrequency = srate;
        mFrameSize = FramesToBytes(1, chans, type);
        mFormat = GetFormat(chans, type);
        if(UNLIKELY(mFormat == AL_NONE))
        {
            auto str = String("Unsupported format (")+GetSampleTypeName(type)+", "+
                       GetChannelConfigName(chans)+")";
            throw std::runtime_error(str);
        }

        mData.resize(mUpdateLen * mFrameSize);
        if(type == SampleType::UInt8) mSilence = 0x80;
        else if(type == SampleType::Mulaw) mSilence = 0x7f;
        else mSilence = 0x00;

        mBufferIds.assign(mNumUpdates, 0);
        alGenBuffers(mBufferIds.size(), mBufferIds.data());
    }

    int64_t getLoopStart() const { return mLoopPts.first; }
    int64_t getLoopEnd() const { return mLoopPts.second; }

    bool hasLooped() const { return mHasLooped; }
    bool hasMoreData() const { return !mDone.load(std::memory_order_acquire); }
    bool streamMoreData(ALuint srcid, bool loop)
    {
        if(mDone.load(std::memory_order_acquire))
            return false;

        ALuint len = mUpdateLen;
        if(loop && mSamplePos <= mLoopPts.second)
            len = static_cast<ALuint>(std::min<uint64_t>(len, mLoopPts.second - mSamplePos));
        else
            loop = false;

        ALuint frames = mDecoder->read(mData.data(), len);
        mSamplePos += frames;
        if(frames < mUpdateLen && loop && mSamplePos > 0)
        {
            if(mSamplePos < mLoopPts.second)
            {
                mLoopPts.second = mSamplePos;
                if(mLoopPts.first >= mLoopPts.second)
                    mLoopPts.first = 0;
            }

            do {
                if(!mDecoder->seek(mLoopPts.first))
                    break;
                mSamplePos = mLoopPts.first;
                mHasLooped = true;

                len = static_cast<ALuint>(
                    std::min<uint64_t>(mUpdateLen-frames, mLoopPts.second-mLoopPts.first)
                );
                ALuint got = mDecoder->read(&mData[frames*mFrameSize], len);
                if(got == 0) break;
                mSamplePos += got;
                frames += got;
            } while(frames < mUpdateLen);
        }
        if(frames < mUpdateLen)
        {
            mDone.store(true, std::memory_order_release);
            if(frames == 0) return false;
            mSamplePos += mUpdateLen - frames;
            std::fill(mData.begin() + frames*mFrameSize, mData.end(), mSilence);
        }

        alBufferData(mBufferIds[mCurrentIdx], mFormat, mData.data(), mData.size(), mFrequency);
        alSourceQueueBuffers(srcid, 1, &mBufferIds[mCurrentIdx]);
        mCurrentIdx = (mCurrentIdx+1) % mBufferIds.size();
        return true;
    }
};


SourceImpl::SourceImpl(ContextImpl &context)
  : mContext(context), mId(0), mBuffer(0), mGroup(nullptr), mIsAsync(false)
  , mDirectFilter(AL_FILTER_NULL)
{
    resetProperties();
    mEffectSlots.reserve(mContext.getDevice().getMaxAuxiliarySends());
}

SourceImpl::~SourceImpl()
{
}


void SourceImpl::resetProperties()
{
    if(mGroup)
        mGroup->eraseSource(this);
    mGroup = nullptr;
    mGroupPitch = 1.0f;
    mGroupGain = 1.0f;

    mFadeGainTarget = 1.0f;
    mFadeGain = 1.0f;

    mPaused.store(false, std::memory_order_release);
    mOffset = 0;
    mPitch = 1.0f;
    mGain = 1.0f;
    mMinGain = 0.0f;
    mMaxGain = 1.0f;
    mRefDist = 1.0f;
    mMaxDist = std::numeric_limits<float>::max();
    mPosition = Vector3(0.0f);
    mVelocity = Vector3(0.0f);
    mDirection = Vector3(0.0f);
    mOrientation[0] = Vector3(0.0f, 0.0f, -1.0f);
    mOrientation[1] = Vector3(0.0f, 1.0f,  0.0f);
    mConeInnerAngle = 360.0f;
    mConeOuterAngle = 360.0f;
    mConeOuterGain = 0.0f;
    mConeOuterGainHF = 1.0f;
    mRolloffFactor = 1.0f;
    mRoomRolloffFactor = 0.0f;
    mDopplerFactor = 1.0f;
    mAirAbsorptionFactor = 0.0f;
    mRadius = 0.0f;
    mStereoAngles[0] =  F_PI / 6.0f;
    mStereoAngles[1] = -F_PI / 6.0f;
    mSpatialize = Spatialize::Auto;
    mResampler = mContext.hasExtension(AL::SOFT_source_resampler) ?
                 alGetInteger(AL_DEFAULT_RESAMPLER_SOFT) : 0;
    mLooping = false;
    mRelative = false;
    mDryGainHFAuto = true;
    mWetGainAuto = true;
    mWetGainHFAuto = true;
    if(mDirectFilter)
        mContext.alDeleteFilters(1, &mDirectFilter);
    mDirectFilter = 0;
    for(auto &i : mEffectSlots)
    {
        if(i.mSlot)
            i.mSlot->removeSourceSend({Source(this), i.mSendIdx});
        if(i.mFilter)
            mContext.alDeleteFilters(1, &i.mFilter);
    }
    mEffectSlots.clear();

    mPriority = 0;
}

void SourceImpl::applyProperties(bool looping, ALuint offset) const
{
    alSourcei(mId, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    alSourcei(mId, AL_SAMPLE_OFFSET, offset);
    alSourcef(mId, AL_PITCH, mPitch * mGroupPitch);
    alSourcef(mId, AL_GAIN, mGain * mGroupGain * mFadeGain);
    alSourcef(mId, AL_MIN_GAIN, mMinGain);
    alSourcef(mId, AL_MAX_GAIN, mMaxGain);
    alSourcef(mId, AL_REFERENCE_DISTANCE, mRefDist);
    alSourcef(mId, AL_MAX_DISTANCE, mMaxDist);
    alSourcefv(mId, AL_POSITION, mPosition.getPtr());
    alSourcefv(mId, AL_VELOCITY, mVelocity.getPtr());
    alSourcefv(mId, AL_DIRECTION, mDirection.getPtr());
    if(mContext.hasExtension(AL::EXT_BFORMAT))
        alSourcefv(mId, AL_ORIENTATION, &mOrientation[0][0]);
    alSourcef(mId, AL_CONE_INNER_ANGLE, mConeInnerAngle);
    alSourcef(mId, AL_CONE_OUTER_ANGLE, mConeOuterAngle);
    alSourcef(mId, AL_CONE_OUTER_GAIN, mConeOuterGain);
    alSourcef(mId, AL_ROLLOFF_FACTOR, mRolloffFactor);
    alSourcef(mId, AL_DOPPLER_FACTOR, mDopplerFactor);
    if(mContext.hasExtension(AL::EXT_SOURCE_RADIUS))
        alSourcef(mId, AL_SOURCE_RADIUS, mRadius);
    if(mContext.hasExtension(AL::EXT_STEREO_ANGLES))
        alSourcefv(mId, AL_STEREO_ANGLES, mStereoAngles);
    if(mContext.hasExtension(AL::SOFT_source_spatialize))
        alSourcei(mId, AL_SOURCE_SPATIALIZE_SOFT, (ALint)mSpatialize);
    if(mContext.hasExtension(AL::SOFT_source_resampler))
        alSourcei(mId, AL_SOURCE_RESAMPLER_SOFT, mResampler);
    alSourcei(mId, AL_SOURCE_RELATIVE, mRelative ? AL_TRUE : AL_FALSE);
    if(mContext.hasExtension(AL::EXT_EFX))
    {
        alSourcef(mId, AL_CONE_OUTER_GAINHF, mConeOuterGainHF);
        alSourcef(mId, AL_ROOM_ROLLOFF_FACTOR, mRoomRolloffFactor);
        alSourcef(mId, AL_AIR_ABSORPTION_FACTOR, mAirAbsorptionFactor);
        alSourcei(mId, AL_DIRECT_FILTER_GAINHF_AUTO, mDryGainHFAuto ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_AUXILIARY_SEND_FILTER_GAIN_AUTO, mWetGainAuto ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO, mWetGainHFAuto ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_DIRECT_FILTER, mDirectFilter);
        for(const auto &i : mEffectSlots)
        {
            ALuint slotid = (i.mSlot ? i.mSlot->getId() : 0);
            alSource3i(mId, AL_AUXILIARY_SEND_FILTER, slotid, i.mSendIdx, i.mFilter);
        }
    }
}


void SourceImpl::unsetGroup()
{
    mGroup = nullptr;
    groupPropUpdate(1.0f, 1.0f);
}

void SourceImpl::groupPropUpdate(ALfloat gain, ALfloat pitch)
{
    if(mId)
    {
        alSourcef(mId, AL_PITCH, mPitch * pitch);
        alSourcef(mId, AL_GAIN, mGain * gain * mFadeGain);
    }
    mGroupPitch = pitch;
    mGroupGain = gain;
}


DECL_THUNK1(void, Source, play,, Buffer)
void SourceImpl::play(Buffer buffer)
{
    BufferImpl *albuf = buffer.getHandle();
    if(!albuf) throw std::invalid_argument("Buffer is not valid");
    CheckContexts(mContext, albuf->getContext());
    CheckContext(mContext);

    if(mStream)
        mContext.removeStream(this);
    mIsAsync.store(false, std::memory_order_release);

    mFadeGainTarget = mFadeGain = 1.0f;
    mFadeTimeTarget = mLastFadeTime = std::chrono::nanoseconds::zero();

    if(mId == 0)
    {
        mId = mContext.getSourceId(mPriority);
        applyProperties(mLooping, (ALuint)std::min<uint64_t>(mOffset, std::numeric_limits<ALint>::max()));
    }
    else
    {
        mContext.removeFadingSource(this);
        mContext.removePlayingSource(this);
        alSourceRewind(mId);
        alSourcei(mId, AL_BUFFER, 0);
        alSourcei(mId, AL_LOOPING, mLooping ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_SAMPLE_OFFSET, (ALuint)std::min<uint64_t>(mOffset, std::numeric_limits<ALint>::max()));
    }
    mOffset = 0;

    mStream.reset();
    if(mBuffer)
        mBuffer->removeSource(Source(this));
    mBuffer = albuf;
    mBuffer->addSource(Source(this));

    alSourcei(mId, AL_BUFFER, mBuffer->getId());
    alSourcePlay(mId);
    mPaused.store(false, std::memory_order_release);
    mContext.removePendingSource(this);
    mContext.addPlayingSource(this, mId);
}

DECL_THUNK3(void, Source, play,, SharedPtr<Decoder>, ALuint, ALuint)
void SourceImpl::play(SharedPtr<Decoder>&& decoder, ALuint chunk_len, ALuint queue_size)
{
    if(chunk_len < 64)
        throw std::out_of_range("Update length out of range");
    if(queue_size < 2)
        throw std::out_of_range("Queue size out of range");
    CheckContext(mContext);

    auto stream = MakeUnique<ALBufferStream>(decoder, chunk_len, queue_size);
    stream->prepare();

    if(mStream)
        mContext.removeStream(this);
    mIsAsync.store(false, std::memory_order_release);

    mFadeGainTarget = mFadeGain = 1.0f;
    mFadeTimeTarget = mLastFadeTime = std::chrono::nanoseconds::zero();

    if(mId == 0)
    {
        mId = mContext.getSourceId(mPriority);
        applyProperties(false, 0);
    }
    else
    {
        mContext.removeFadingSource(this);
        mContext.removePlayingSource(this);
        alSourceRewind(mId);
        alSourcei(mId, AL_BUFFER, 0);
        alSourcei(mId, AL_LOOPING, AL_FALSE);
        alSourcei(mId, AL_SAMPLE_OFFSET, 0);
    }

    mStream.reset();
    if(mBuffer)
        mBuffer->removeSource(Source(this));
    mBuffer = 0;

    mStream = std::move(stream);

    mStream->seek(mOffset);
    mOffset = 0;

    for(ALuint i = 0;i < mStream->getNumUpdates();i++)
    {
        if(!mStream->streamMoreData(mId, mLooping))
            break;
    }
    alSourcePlay(mId);
    mPaused.store(false, std::memory_order_release);

    mContext.addStream(this);
    mIsAsync.store(true, std::memory_order_release);
    mContext.removePendingSource(this);
    mContext.addPlayingSource(this);
}

DECL_THUNK1(void, Source, play,, SharedFuture<Buffer>)
void SourceImpl::play(SharedFuture<Buffer>&& future_buffer)
{
    if(!future_buffer.valid())
        throw std::future_error(std::future_errc::no_state);
    if(GetFutureState(future_buffer) == std::future_status::ready)
    {
        play(future_buffer.get());
        return;
    }

    CheckContext(mContext);

    mContext.removeFadingSource(this);
    mContext.removePlayingSource(this);
    makeStopped(true);

    mFadeGainTarget = mFadeGain = 1.0f;
    mFadeTimeTarget = mLastFadeTime = std::chrono::nanoseconds::zero();

    mContext.addPendingSource(this, std::move(future_buffer));
}


DECL_THUNK0(void, Source, stop,)
void SourceImpl::stop()
{
    CheckContext(mContext);
    mContext.removePendingSource(this);
    mContext.removeFadingSource(this);
    mContext.removePlayingSource(this);
    makeStopped();
}

void SourceImpl::makeStopped(bool dolock)
{
    if(mStream)
    {
        if(dolock)
            mContext.removeStream(this);
        else
            mContext.removeStreamNoLock(this);
    }
    mIsAsync.store(false, std::memory_order_release);

    if(mId != 0)
    {
        alSourceRewind(mId);
        alSourcei(mId, AL_BUFFER, 0);
        if(mContext.hasExtension(AL::EXT_EFX))
        {
            alSourcei(mId, AL_DIRECT_FILTER, AL_FILTER_NULL);
            for(auto &i : mEffectSlots)
                alSource3i(mId, AL_AUXILIARY_SEND_FILTER, 0, i.mSendIdx, AL_FILTER_NULL);
        }
        mContext.insertSourceId(mId);
        mId = 0;
    }

    mStream.reset();
    if(mBuffer)
        mBuffer->removeSource(Source(this));
    mBuffer = 0;

    mPaused.store(false, std::memory_order_release);
}


DECL_THUNK2(void, Source, fadeOutToStop,, ALfloat, std::chrono::milliseconds)
void SourceImpl::fadeOutToStop(ALfloat gain, std::chrono::milliseconds duration)
{
    if(!(gain < 1.0f && gain >= 0.0f))
        throw std::out_of_range("Fade gain target out of range");
    if(duration.count() <= 0)
        throw std::out_of_range("Fade duration out of range");
    CheckContext(mContext);

    mFadeGainTarget = std::max<ALfloat>(gain, 0.0001f);
    mLastFadeTime = std::chrono::steady_clock::now().time_since_epoch();
    mFadeTimeTarget = mLastFadeTime + duration;

    mContext.addFadingSource(this);
}


void SourceImpl::checkPaused()
{
    if(mPaused.load(std::memory_order_acquire) || mId == 0)
        return;

    ALint state = -1;
    alGetSourcei(mId, AL_SOURCE_STATE, &state);
    // Streaming sources may be in a stopped or initial state if underrun
    mPaused.store(state == AL_PAUSED || (mStream && mStream->hasMoreData()),
                  std::memory_order_release);
}

DECL_THUNK0(void, Source, pause,)
void SourceImpl::pause()
{
    CheckContext(mContext);
    if(mPaused.load(std::memory_order_acquire))
        return;

    if(mId != 0)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        alSourcePause(mId);
        ALint state = -1;
        alGetSourcei(mId, AL_SOURCE_STATE, &state);
        // Streaming sources may be in a stopped or initial state if underrun
        mPaused.store(state == AL_PAUSED || (mStream && mStream->hasMoreData()),
                      std::memory_order_release);
    }
}

DECL_THUNK0(void, Source, resume,)
void SourceImpl::resume()
{
    CheckContext(mContext);
    if(!mPaused.load(std::memory_order_acquire))
        return;

    if(mId != 0)
        alSourcePlay(mId);
    mPaused.store(false, std::memory_order_release);
}


DECL_THUNK0(bool, Source, isPending, const)
bool SourceImpl::isPending() const
{
    CheckContext(mContext);
    return mContext.isPendingSource(this);
}

DECL_THUNK0(bool, Source, isPlaying, const)
bool SourceImpl::isPlaying() const
{
    CheckContext(mContext);
    if(mId == 0) return false;

    ALint state = -1;
    alGetSourcei(mId, AL_SOURCE_STATE, &state);
    if(state == -1)
        throw std::runtime_error("Source state error");

    return state == AL_PLAYING || (!mPaused.load(std::memory_order_acquire) &&
                                   mStream && mStream->hasMoreData());
}

DECL_THUNK0(bool, Source, isPaused, const)
bool SourceImpl::isPaused() const
{
    CheckContext(mContext);
    return mId != 0 && mPaused.load(std::memory_order_acquire);
}


DECL_THUNK1(void, Source, setGroup,, SourceGroup)
void SourceImpl::setGroup(SourceGroup group)
{
    CheckContext(mContext);

    SourceGroupImpl *parent = group.getHandle();
    if(parent == mGroup) return;

    if(mGroup)
        mGroup->eraseSource(this);
    mGroup = parent;
    if(mGroup)
    {
        mGroup->insertSource(this);
        mGroupPitch = mGroup->getAppliedPitch();
        mGroupGain = mGroup->getAppliedGain();
    }
    else
    {
        mGroupPitch = 1.0f;
        mGroupGain = 1.0f;
    }

    if(mId)
    {
        alSourcef(mId, AL_PITCH, mPitch * mGroupPitch);
        alSourcef(mId, AL_GAIN, mGain * mGroupGain * mFadeGain);
    }
}


bool SourceImpl::checkPending(SharedFuture<Buffer> &future)
{
    if(GetFutureState(future) != std::future_status::ready)
        return true;

    BufferImpl *buffer = future.get().getHandle();
    if(UNLIKELY(!buffer || &(buffer->getContext()) != &mContext))
        return false;

    if(mId == 0)
    {
        mId = mContext.getSourceId(mPriority);
        applyProperties(mLooping, (ALuint)std::min<uint64_t>(mOffset, std::numeric_limits<ALint>::max()));
    }
    else
    {
        alSourceRewind(mId);
        alSourcei(mId, AL_BUFFER, 0);
        alSourcei(mId, AL_LOOPING, mLooping ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_SAMPLE_OFFSET, (ALuint)std::min<uint64_t>(mOffset, std::numeric_limits<ALint>::max()));
    }
    mOffset = 0;

    mBuffer = buffer;
    mBuffer->addSource(Source(this));

    alSourcei(mId, AL_BUFFER, mBuffer->getId());
    alSourcePlay(mId);
    mPaused.store(false, std::memory_order_release);
    mContext.addPlayingSource(this, mId);
    return false;
}

bool SourceImpl::fadeUpdate(std::chrono::nanoseconds cur_fade_time)
{
    if((cur_fade_time - mFadeTimeTarget).count() >= 0)
    {
        mLastFadeTime = mFadeTimeTarget;
        mFadeGain = 1.0f;
        if(mFadeGainTarget >= 1.0f)
        {
            if(mId != 0)
                alSourcef(mId, AL_GAIN, mGain * mGroupGain);
            return false;
        }
        mContext.removePendingSource(this);
        mContext.removePlayingSource(this);
        makeStopped(true);
        return false;
    }

    float mult = std::pow(mFadeGainTarget/mFadeGain,
        float(1.0/Seconds(mFadeTimeTarget-mLastFadeTime).count())
    );

    std::chrono::nanoseconds duration = cur_fade_time - mLastFadeTime;
    mLastFadeTime = cur_fade_time;

    float gain = mFadeGain * std::pow(mult, (float)Seconds(duration).count());
    if(UNLIKELY(gain == mFadeGain))
    {
        // Ensure the gain keeps moving toward its target, in case precision
        // loss results in no change with small steps.
        gain = std::nextafter(gain, mFadeGainTarget);
    }
    mFadeGain = gain;

    if(mId != 0)
        alSourcef(mId, AL_GAIN, mGain * mGroupGain * mFadeGain);
    return true;
}

bool SourceImpl::playUpdate(ALuint id)
{
    ALint state = -1;
    alGetSourcei(id, AL_SOURCE_STATE, &state);
    if(LIKELY(state == AL_PLAYING || state == AL_PAUSED))
        return true;

    makeStopped();
    mContext.send(&MessageHandler::sourceStopped, Source(this));
    return false;
}

bool SourceImpl::playUpdate()
{
    if(LIKELY(mIsAsync.load(std::memory_order_acquire)))
        return true;

    makeStopped();
    mContext.send(&MessageHandler::sourceStopped, Source(this));
    return false;
}


ALint SourceImpl::refillBufferStream()
{
    ALint processed;
    alGetSourcei(mId, AL_BUFFERS_PROCESSED, &processed);
    while(processed > 0)
    {
        ALuint buf;
        alSourceUnqueueBuffers(mId, 1, &buf);
        --processed;
    }

    ALint queued;
    alGetSourcei(mId, AL_BUFFERS_QUEUED, &queued);
    for(;(ALuint)queued < mStream->getNumUpdates();queued++)
    {
        if(!mStream->streamMoreData(mId, mLooping))
            break;
    }

    return queued;
}

bool SourceImpl::updateAsync()
{
    std::lock_guard<std::mutex> lock(mMutex);

    ALint queued = refillBufferStream();
    if(queued == 0)
    {
        mIsAsync.store(false, std::memory_order_release);
        return false;
    }

    ALint state = -1;
    alGetSourcei(mId, AL_SOURCE_STATE, &state);
    if(!mPaused.load(std::memory_order_acquire))
    {
        // Make sure the source is still playing if it's not paused.
        if(state != AL_PLAYING)
            alSourcePlay(mId);
    }
    else
    {
        // Rewind the source to an initial state if it underrun as it was
        // paused.
        if(state == AL_STOPPED)
            alSourceRewind(mId);
    }
    return true;
}


DECL_THUNK1(void, Source, setPriority,, ALuint)
void SourceImpl::setPriority(ALuint priority)
{
    mPriority = priority;
}


DECL_THUNK1(void, Source, setOffset,, uint64_t)
void SourceImpl::setOffset(uint64_t offset)
{
    CheckContext(mContext);
    if(mId == 0)
    {
        mOffset = offset;
        return;
    }

    if(!mStream)
    {
        if(offset >= std::numeric_limits<ALint>::max())
            throw std::out_of_range("Offset out of range");
        alGetError();
        alSourcei(mId, AL_SAMPLE_OFFSET, (ALint)offset);
        throw_al_error("Failed to set offset");
    }
    else
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if(!mStream->seek(offset))
            throw std::runtime_error("Failed to seek to offset");
        alSourceRewind(mId);
        alSourcei(mId, AL_BUFFER, 0);
        ALint queued = refillBufferStream();
        if(queued > 0 && !mPaused.load(std::memory_order_acquire))
            alSourcePlay(mId);
    }
}

DECL_THUNK0(UInt64NSecPair, Source, getSampleOffsetLatency, const)
std::pair<uint64_t,std::chrono::nanoseconds> SourceImpl::getSampleOffsetLatency() const
{
    std::pair<uint64_t,std::chrono::nanoseconds> ret{0, std::chrono::nanoseconds::zero()};
    CheckContext(mContext);
    if(mId == 0) return ret;

    if(mStream)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        ALint queued = 0, state = -1, srcpos = 0;

        alGetSourcei(mId, AL_BUFFERS_QUEUED, &queued);
        if(mContext.hasExtension(AL::SOFT_source_latency))
        {
            ALint64SOFT val[2];
            mContext.alGetSourcei64vSOFT(mId, AL_SAMPLE_OFFSET_LATENCY_SOFT, val);
            srcpos = val[0]>>32;
            ret.second = std::chrono::nanoseconds(val[1]);
        }
        else
            alGetSourcei(mId, AL_SAMPLE_OFFSET, &srcpos);
        alGetSourcei(mId, AL_SOURCE_STATE, &state);

        int64_t streampos = mStream->getPosition();
        if(state != AL_STOPPED)
        {
            // The amount of samples in the queue waiting to play
            ALuint inqueue = queued*mStream->getUpdateLength() - srcpos;
            if(!mStream->hasLooped())
            {
                // A non-looped stream should never have more samples queued
                // than have been read...
                streampos = std::max<int64_t>(streampos, inqueue) - inqueue;
            }
            else
            {
                streampos -= inqueue;
                int64_t looplen = mStream->getLoopEnd() - mStream->getLoopStart();
                while(streampos < mStream->getLoopStart())
                    streampos += looplen;
            }
        }

        ret.first = streampos;
        return ret;
    }

    ALint srcpos = 0;
    if(mContext.hasExtension(AL::SOFT_source_latency))
    {
        ALint64SOFT val[2];
        mContext.alGetSourcei64vSOFT(mId, AL_SAMPLE_OFFSET_LATENCY_SOFT, val);
        srcpos = val[0]>>32;
        ret.second = std::chrono::nanoseconds(val[1]);
    }
    else
        alGetSourcei(mId, AL_SAMPLE_OFFSET, &srcpos);
    ret.first = srcpos;
    return ret;
}

DECL_THUNK0(SecondsPair, Source, getSecOffsetLatency, const)
std::pair<Seconds,Seconds> SourceImpl::getSecOffsetLatency() const
{
    std::pair<Seconds,Seconds> ret{Seconds::zero(), Seconds::zero()};
    CheckContext(mContext);
    if(mId == 0) return ret;

    if(mStream)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        ALint queued = 0, state = -1;
        ALdouble srcpos = 0;

        alGetSourcei(mId, AL_BUFFERS_QUEUED, &queued);
        if(mContext.hasExtension(AL::SOFT_source_latency))
        {
            ALdouble val[2];
            mContext.alGetSourcedvSOFT(mId, AL_SEC_OFFSET_LATENCY_SOFT, val);
            srcpos = val[0];
            ret.second = Seconds(val[1]);
        }
        else
        {
            ALfloat f;
            alGetSourcef(mId, AL_SEC_OFFSET, &f);
            srcpos = f;
        }
        alGetSourcei(mId, AL_SOURCE_STATE, &state);

        ALdouble frac = 0.0;
        int64_t streampos = mStream->getPosition();
        if(state != AL_STOPPED)
        {
            ALdouble ipos;
            frac = std::modf(srcpos * mStream->getFrequency(), &ipos);

            // The amount of samples in the queue waiting to play
            ALuint inqueue = queued*mStream->getUpdateLength() - (ALuint)ipos;
            if(!mStream->hasLooped())
            {
                // A non-looped stream should never have more samples queued
                // than have been read...
                streampos = std::max<int64_t>(streampos, inqueue) - inqueue;
            }
            else
            {
                streampos -= inqueue;
                int64_t looplen = mStream->getLoopEnd() - mStream->getLoopStart();
                while(streampos < mStream->getLoopStart())
                    streampos += looplen;
            }
        }

        ret.first = Seconds((streampos+frac) / mStream->getFrequency());
        return ret;
    }

    if(mContext.hasExtension(AL::SOFT_source_latency))
    {
        ALdouble val[2];
        mContext.alGetSourcedvSOFT(mId, AL_SEC_OFFSET_LATENCY_SOFT, val);
        ret.first = Seconds(val[0]);
        ret.second = Seconds(val[1]);
    }
    else
    {
        ALfloat f;
        alGetSourcef(mId, AL_SEC_OFFSET, &f);
        ret.first = Seconds(f);
    }
    return ret;
}


DECL_THUNK1(void, Source, setLooping,, bool)
void SourceImpl::setLooping(bool looping)
{
    CheckContext(mContext);

    if(mId && !mStream)
        alSourcei(mId, AL_LOOPING, looping ? AL_TRUE : AL_FALSE);
    mLooping = looping;
}


DECL_THUNK1(void, Source, setPitch,, ALfloat)
void SourceImpl::setPitch(ALfloat pitch)
{
    if(!(pitch > 0.0f))
        throw std::out_of_range("Pitch out of range");
    CheckContext(mContext);
    if(mId != 0)
        alSourcef(mId, AL_PITCH, pitch * mGroupPitch);
    mPitch = pitch;
}


DECL_THUNK1(void, Source, setGain,, ALfloat)
void SourceImpl::setGain(ALfloat gain)
{
    if(!(gain >= 0.0f))
        throw std::out_of_range("Gain out of range");
    CheckContext(mContext);
    if(mId != 0)
        alSourcef(mId, AL_GAIN, gain * mGroupGain * mFadeGain);
    mGain = gain;
}

DECL_THUNK2(void, Source, setGainRange,, ALfloat, ALfloat)
void SourceImpl::setGainRange(ALfloat mingain, ALfloat maxgain)
{
    if(!(mingain >= 0.0f && maxgain <= 1.0f && maxgain >= mingain))
        throw std::out_of_range("Gain range out of range");
    CheckContext(mContext);
    if(mId != 0)
    {
        alSourcef(mId, AL_MIN_GAIN, mingain);
        alSourcef(mId, AL_MAX_GAIN, maxgain);
    }
    mMinGain = mingain;
    mMaxGain = maxgain;
}


DECL_THUNK2(void, Source, setDistanceRange,, ALfloat, ALfloat)
void SourceImpl::setDistanceRange(ALfloat refdist, ALfloat maxdist)
{
    if(!(refdist >= 0.0f && maxdist <= std::numeric_limits<float>::max() && refdist <= maxdist))
        throw std::out_of_range("Distance range out of range");
    CheckContext(mContext);
    if(mId != 0)
    {
        alSourcef(mId, AL_REFERENCE_DISTANCE, refdist);
        alSourcef(mId, AL_MAX_DISTANCE, maxdist);
    }
    mRefDist = refdist;
    mMaxDist = maxdist;
}


DECL_THUNK3(void, Source, set3DParameters,, const Vector3&, const Vector3&, const Vector3&)
void SourceImpl::set3DParameters(const Vector3 &position, const Vector3 &velocity, const Vector3 &direction)
{
    CheckContext(mContext);
    if(mId != 0)
    {
        Batcher batcher = mContext.getBatcher();
        alSourcefv(mId, AL_POSITION, position.getPtr());
        alSourcefv(mId, AL_VELOCITY, velocity.getPtr());
        alSourcefv(mId, AL_DIRECTION, direction.getPtr());
    }
    mPosition = position;
    mVelocity = velocity;
    mDirection = direction;
}

DECL_THUNK3(void, Source, set3DParameters,, const Vector3&, const Vector3&, const Vector3Pair&)
void SourceImpl::set3DParameters(const Vector3 &position, const Vector3 &velocity, const std::pair<Vector3,Vector3> &orientation)
{
    static_assert(sizeof(orientation) == sizeof(ALfloat[6]), "Invalid Vector3 pair size");
    CheckContext(mContext);
    if(mId != 0)
    {
        Batcher batcher = mContext.getBatcher();
        alSourcefv(mId, AL_POSITION, position.getPtr());
        alSourcefv(mId, AL_VELOCITY, velocity.getPtr());
        if(mContext.hasExtension(AL::EXT_BFORMAT))
            alSourcefv(mId, AL_ORIENTATION, orientation.first.getPtr());
        alSourcefv(mId, AL_DIRECTION, orientation.first.getPtr());
    }
    mPosition = position;
    mVelocity = velocity;
    mDirection = mOrientation[0] = orientation.first;
    mOrientation[1] = orientation.second;
}


DECL_THUNK1(void, Source, setPosition,, const Vector3&)
void SourceImpl::setPosition(const Vector3 &position)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_POSITION, position.getPtr());
    mPosition = position;
}

DECL_THUNK1(void, Source, setPosition,, const ALfloat*)
void SourceImpl::setPosition(const ALfloat *pos)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_POSITION, pos);
    mPosition[0] = pos[0];
    mPosition[1] = pos[1];
    mPosition[2] = pos[2];
}

DECL_THUNK1(void, Source, setVelocity,, const Vector3&)
void SourceImpl::setVelocity(const Vector3 &velocity)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_VELOCITY, velocity.getPtr());
    mVelocity = velocity;
}

DECL_THUNK1(void, Source, setVelocity,, const ALfloat*)
void SourceImpl::setVelocity(const ALfloat *vel)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_VELOCITY, vel);
    mVelocity[0] = vel[0];
    mVelocity[1] = vel[1];
    mVelocity[2] = vel[2];
}

DECL_THUNK1(void, Source, setDirection,, const Vector3&)
void SourceImpl::setDirection(const Vector3 &direction)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_DIRECTION, direction.getPtr());
    mDirection = direction;
}

DECL_THUNK1(void, Source, setDirection,, const ALfloat*)
void SourceImpl::setDirection(const ALfloat *dir)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcefv(mId, AL_DIRECTION, dir);
    mDirection[0] = dir[0];
    mDirection[1] = dir[1];
    mDirection[2] = dir[2];
}

DECL_THUNK1(void, Source, setOrientation,, const Vector3Pair&)
void SourceImpl::setOrientation(const std::pair<Vector3,Vector3> &orientation)
{
    CheckContext(mContext);
    if(mId != 0)
    {
        if(mContext.hasExtension(AL::EXT_BFORMAT))
            alSourcefv(mId, AL_ORIENTATION, orientation.first.getPtr());
        alSourcefv(mId, AL_DIRECTION, orientation.first.getPtr());
    }
    mDirection = mOrientation[0] = orientation.first;
    mOrientation[1] = orientation.second;
}

DECL_THUNK2(void, Source, setOrientation,, const ALfloat*, const ALfloat*)
void SourceImpl::setOrientation(const ALfloat *at, const ALfloat *up)
{
    CheckContext(mContext);
    if(mId != 0)
    {
        ALfloat ori[6] = { at[0], at[1], at[2], up[0], up[1], up[2] };
        if(mContext.hasExtension(AL::EXT_BFORMAT))
            alSourcefv(mId, AL_ORIENTATION, ori);
        alSourcefv(mId, AL_DIRECTION, ori);
    }
    mDirection[0] = mOrientation[0][0] = at[0];
    mDirection[1] = mOrientation[0][1] = at[1];
    mDirection[2] = mOrientation[0][2] = at[2];
    mOrientation[1][0] = up[0];
    mOrientation[1][1] = up[1];
    mOrientation[1][2] = up[2];
}

DECL_THUNK1(void, Source, setOrientation,, const ALfloat*)
void SourceImpl::setOrientation(const ALfloat *ori)
{
    CheckContext(mContext);
    if(mId != 0)
    {
        if(mContext.hasExtension(AL::EXT_BFORMAT))
            alSourcefv(mId, AL_ORIENTATION, ori);
        alSourcefv(mId, AL_DIRECTION, ori);
    }
    mDirection[0] = mOrientation[0][0] = ori[0];
    mDirection[1] = mOrientation[0][1] = ori[1];
    mDirection[2] = mOrientation[0][2] = ori[2];
    mOrientation[1][0] = ori[3];
    mOrientation[1][1] = ori[4];
    mOrientation[1][2] = ori[5];
}


DECL_THUNK2(void, Source, setConeAngles,, ALfloat, ALfloat)
void SourceImpl::setConeAngles(ALfloat inner, ALfloat outer)
{
    if(!(inner >= 0.0f && outer <= 360.0f && outer >= inner))
        throw std::out_of_range("Cone angles out of range");
    CheckContext(mContext);
    if(mId != 0)
    {
        alSourcef(mId, AL_CONE_INNER_ANGLE, inner);
        alSourcef(mId, AL_CONE_OUTER_ANGLE, outer);
    }
    mConeInnerAngle = inner;
    mConeOuterAngle = outer;
}

DECL_THUNK2(void, Source, setOuterConeGains,, ALfloat, ALfloat)
void SourceImpl::setOuterConeGains(ALfloat gain, ALfloat gainhf)
{
    if(!(gain >= 0.0f && gain <= 1.0f && gainhf >= 0.0f && gainhf <= 1.0f))
        throw std::out_of_range("Outer cone gain out of range");
    CheckContext(mContext);
    if(mId != 0)
    {
        alSourcef(mId, AL_CONE_OUTER_GAIN, gain);
        if(mContext.hasExtension(AL::EXT_EFX))
            alSourcef(mId, AL_CONE_OUTER_GAINHF, gainhf);
    }
    mConeOuterGain = gain;
    mConeOuterGainHF = gainhf;
}


DECL_THUNK2(void, Source, setRolloffFactors,, ALfloat, ALfloat)
void SourceImpl::setRolloffFactors(ALfloat factor, ALfloat roomfactor)
{
    if(!(factor >= 0.0f && roomfactor >= 0.0f))
        throw std::out_of_range("Rolloff factor out of range");
    CheckContext(mContext);
    if(mId != 0)
    {
        alSourcef(mId, AL_ROLLOFF_FACTOR, factor);
        if(mContext.hasExtension(AL::EXT_EFX))
            alSourcef(mId, AL_ROOM_ROLLOFF_FACTOR, roomfactor);
    }
    mRolloffFactor = factor;
    mRoomRolloffFactor = roomfactor;
}

DECL_THUNK1(void, Source, setDopplerFactor,, ALfloat)
void SourceImpl::setDopplerFactor(ALfloat factor)
{
    if(!(factor >= 0.0f && factor <= 1.0f))
        throw std::out_of_range("Doppler factor out of range");
    CheckContext(mContext);
    if(mId != 0)
        alSourcef(mId, AL_DOPPLER_FACTOR, factor);
    mDopplerFactor = factor;
}

DECL_THUNK1(void, Source, setRelative,, bool)
void SourceImpl::setRelative(bool relative)
{
    CheckContext(mContext);
    if(mId != 0)
        alSourcei(mId, AL_SOURCE_RELATIVE, relative ? AL_TRUE : AL_FALSE);
    mRelative = relative;
}

DECL_THUNK1(void, Source, setRadius,, ALfloat)
void SourceImpl::setRadius(ALfloat radius)
{
    if(!(mRadius >= 0.0f))
        throw std::out_of_range("Radius out of range");
    CheckContext(mContext);
    if(mId != 0 && mContext.hasExtension(AL::EXT_SOURCE_RADIUS))
        alSourcef(mId, AL_SOURCE_RADIUS, radius);
    mRadius = radius;
}

DECL_THUNK2(void, Source, setStereoAngles,, ALfloat, ALfloat)
void SourceImpl::setStereoAngles(ALfloat leftAngle, ALfloat rightAngle)
{
    CheckContext(mContext);
    if(mId != 0 && mContext.hasExtension(AL::EXT_STEREO_ANGLES))
    {
        ALfloat angles[2] = { leftAngle, rightAngle };
        alSourcefv(mId, AL_STEREO_ANGLES, angles);
    }
    mStereoAngles[0] = leftAngle;
    mStereoAngles[1] = rightAngle;
}

DECL_THUNK1(void, Source, set3DSpatialize,, Spatialize)
void SourceImpl::set3DSpatialize(Spatialize spatialize)
{
    CheckContext(mContext);
    if(mId != 0 && mContext.hasExtension(AL::SOFT_source_spatialize))
        alSourcei(mId, AL_SOURCE_SPATIALIZE_SOFT, (ALint)spatialize);
    mSpatialize = spatialize;
}

DECL_THUNK1(void, Source, setResamplerIndex,, ALsizei)
void SourceImpl::setResamplerIndex(ALsizei index)
{
    if(index < 0)
        throw std::out_of_range("Resampler index out of range");
    index = std::min<ALsizei>(index, mContext.getAvailableResamplers().size());
    if(mId != 0 && mContext.hasExtension(AL::SOFT_source_resampler))
        alSourcei(mId, AL_SOURCE_RESAMPLER_SOFT, index);
    mResampler = index;
}

DECL_THUNK1(void, Source, setAirAbsorptionFactor,, ALfloat)
void SourceImpl::setAirAbsorptionFactor(ALfloat factor)
{
    if(!(factor >= 0.0f && factor <= 10.0f))
        throw std::out_of_range("Absorption factor out of range");
    CheckContext(mContext);
    if(mId != 0 && mContext.hasExtension(AL::EXT_EFX))
        alSourcef(mId, AL_AIR_ABSORPTION_FACTOR, factor);
    mAirAbsorptionFactor = factor;
}

DECL_THUNK3(void, Source, setGainAuto,, bool, bool, bool)
void SourceImpl::setGainAuto(bool directhf, bool send, bool sendhf)
{
    CheckContext(mContext);
    if(mId != 0 && mContext.hasExtension(AL::EXT_EFX))
    {
        alSourcei(mId, AL_DIRECT_FILTER_GAINHF_AUTO, directhf ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_AUXILIARY_SEND_FILTER_GAIN_AUTO, send ? AL_TRUE : AL_FALSE);
        alSourcei(mId, AL_AUXILIARY_SEND_FILTER_GAINHF_AUTO, sendhf ? AL_TRUE : AL_FALSE);
    }
    mDryGainHFAuto = directhf;
    mWetGainAuto = send;
    mWetGainHFAuto = sendhf;
}


void SourceImpl::setFilterParams(ALuint &filterid, const FilterParams &params)
{
    if(!mContext.hasExtension(AL::EXT_EFX))
        return;

    if(!(params.mGain < 1.0f || params.mGainHF < 1.0f || params.mGainLF < 1.0f))
    {
        if(filterid)
            mContext.alFilteri(filterid, AL_FILTER_TYPE, AL_FILTER_NULL);
        return;
    }

    alGetError();
    if(!filterid)
    {
        mContext.alGenFilters(1, &filterid);
        throw_al_error("Failed to create Filter");
    }
    bool filterset = false;
    if(params.mGainHF < 1.0f && params.mGainLF < 1.0f)
    {
        mContext.alFilteri(filterid, AL_FILTER_TYPE, AL_FILTER_BANDPASS);
        if(alGetError() == AL_NO_ERROR)
        {
            mContext.alFilterf(filterid, AL_BANDPASS_GAIN, std::min(params.mGain, 1.0f));
            mContext.alFilterf(filterid, AL_BANDPASS_GAINHF, std::min(params.mGainHF, 1.0f));
            mContext.alFilterf(filterid, AL_BANDPASS_GAINLF, std::min(params.mGainLF, 1.0f));
            filterset = true;
        }
    }
    if(!filterset && !(params.mGainHF < 1.0f) && params.mGainLF < 1.0f)
    {
        mContext.alFilteri(filterid, AL_FILTER_TYPE, AL_FILTER_HIGHPASS);
        if(alGetError() == AL_NO_ERROR)
        {
            mContext.alFilterf(filterid, AL_HIGHPASS_GAIN, std::min(params.mGain, 1.0f));
            mContext.alFilterf(filterid, AL_HIGHPASS_GAINLF, std::min(params.mGainLF, 1.0f));
            filterset = true;
        }
    }
    if(!filterset)
    {
        mContext.alFilteri(filterid, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        if(alGetError() == AL_NO_ERROR)
        {
            mContext.alFilterf(filterid, AL_LOWPASS_GAIN, std::min(params.mGain, 1.0f));
            mContext.alFilterf(filterid, AL_LOWPASS_GAINHF, std::min(params.mGainHF, 1.0f));
            filterset = true;
        }
    }
}


DECL_THUNK1(void, Source, setDirectFilter,, const FilterParams&)
void SourceImpl::setDirectFilter(const FilterParams &filter)
{
    if(!(filter.mGain >= 0.0f && filter.mGainHF >= 0.0f && filter.mGainLF >= 0.0f))
        throw std::out_of_range("Gain value out of range");
    CheckContext(mContext);

    setFilterParams(mDirectFilter, filter);
    if(mId)
        alSourcei(mId, AL_DIRECT_FILTER, mDirectFilter);
}

DECL_THUNK2(void, Source, setSendFilter,, ALuint, const FilterParams&)
void SourceImpl::setSendFilter(ALuint send, const FilterParams &filter)
{
    if(!(filter.mGain >= 0.0f && filter.mGainHF >= 0.0f && filter.mGainLF >= 0.0f))
        throw std::out_of_range("Gain value out of range");
    CheckContext(mContext);

    auto siter = std::lower_bound(mEffectSlots.begin(), mEffectSlots.end(), send,
        [](const SendProps &prop, ALuint send) -> bool
        { return prop.mSendIdx < send; }
    );
    if(siter == mEffectSlots.end() || siter->mSendIdx != send)
    {
        ALuint filterid = 0;

        setFilterParams(filterid, filter);
        if(!filterid) return;

        siter = mEffectSlots.emplace(siter, send, filterid);
    }
    else
        setFilterParams(siter->mFilter, filter);

    if(mId)
    {
        ALuint slotid = (siter->mSlot ? siter->mSlot->getId() : 0);
        alSource3i(mId, AL_AUXILIARY_SEND_FILTER, slotid, send, siter->mFilter);
    }
}

DECL_THUNK2(void, Source, setAuxiliarySend,, AuxiliaryEffectSlot, ALuint)
void SourceImpl::setAuxiliarySend(AuxiliaryEffectSlot auxslot, ALuint send)
{
    AuxiliaryEffectSlotImpl *slot = auxslot.getHandle();
    if(slot) CheckContexts(mContext, slot->getContext());
    CheckContext(mContext);

    auto siter = std::lower_bound(mEffectSlots.begin(), mEffectSlots.end(), send,
        [](const SendProps &prop, ALuint send) -> bool
        { return prop.mSendIdx < send; }
    );
    if(siter == mEffectSlots.end() || siter->mSendIdx != send)
    {
        if(!slot) return;
        slot->addSourceSend({Source(this), send});
        siter = mEffectSlots.emplace(siter, send, slot);
    }
    else if(siter->mSlot != slot)
    {
        if(slot) slot->addSourceSend({Source(this), send});
        if(siter->mSlot)
            siter->mSlot->removeSourceSend({Source(this), send});
        siter->mSlot = slot;
    }

    if(mId)
    {
        ALuint slotid = (siter->mSlot ? siter->mSlot->getId() : 0);
        alSource3i(mId, AL_AUXILIARY_SEND_FILTER, slotid, send, siter->mFilter);
    }
}

DECL_THUNK3(void, Source, setAuxiliarySendFilter,, AuxiliaryEffectSlot, ALuint, const FilterParams&)
void SourceImpl::setAuxiliarySendFilter(AuxiliaryEffectSlot auxslot, ALuint send, const FilterParams &filter)
{
    if(!(filter.mGain >= 0.0f && filter.mGainHF >= 0.0f && filter.mGainLF >= 0.0f))
        throw std::out_of_range("Gain value out of range");
    AuxiliaryEffectSlotImpl *slot = auxslot.getHandle();
    if(slot) CheckContexts(mContext, slot->getContext());
    CheckContext(mContext);

    auto siter = std::lower_bound(mEffectSlots.begin(), mEffectSlots.end(), send,
        [](const SendProps &prop, ALuint send) -> bool
        { return prop.mSendIdx < send; }
    );
    if(siter == mEffectSlots.end() || siter->mSendIdx != send)
    {
        ALuint filterid = 0;

        setFilterParams(filterid, filter);
        if(!filterid && !slot)
            return;

        if(slot) slot->addSourceSend({Source(this), send});
        siter = mEffectSlots.emplace(siter, send, slot, filterid);
    }
    else
    {
        if(siter->mSlot != slot)
        {
            if(slot) slot->addSourceSend({Source(this), send});
            if(siter->mSlot)
                siter->mSlot->removeSourceSend({Source(this), send});
            siter->mSlot = slot;
        }
        setFilterParams(siter->mFilter, filter);
    }

    if(mId)
    {
        ALuint slotid = (siter->mSlot ? siter->mSlot->getId() : 0);
        alSource3i(mId, AL_AUXILIARY_SEND_FILTER, slotid, send, siter->mFilter);
    }
}


void Source::release()
{
    SourceImpl *i = pImpl;
    pImpl = nullptr;
    i->release();
}
void SourceImpl::release()
{
    stop();

    resetProperties();
    mContext.freeSource(this);
}


DECL_THUNK0(SourceGroup, Source, getGroup, const)
DECL_THUNK0(ALuint, Source, getPriority, const)
DECL_THUNK0(bool, Source, getLooping, const)
DECL_THUNK0(ALfloat, Source, getPitch, const)
DECL_THUNK0(ALfloat, Source, getGain, const)
DECL_THUNK0(ALfloatPair, Source, getGainRange, const)
DECL_THUNK0(ALfloatPair, Source, getDistanceRange, const)
DECL_THUNK0(Vector3, Source, getPosition, const)
DECL_THUNK0(Vector3, Source, getVelocity, const)
DECL_THUNK0(Vector3, Source, getDirection, const)
DECL_THUNK0(Vector3Pair, Source, getOrientation, const)
DECL_THUNK0(ALfloatPair, Source, getConeAngles, const)
DECL_THUNK0(ALfloatPair, Source, getOuterConeGains, const)
DECL_THUNK0(ALfloatPair, Source, getRolloffFactors, const)
DECL_THUNK0(ALfloat, Source, getDopplerFactor, const)
DECL_THUNK0(bool, Source, getRelative, const)
DECL_THUNK0(ALfloat, Source, getRadius, const)
DECL_THUNK0(ALfloatPair, Source, getStereoAngles, const)
DECL_THUNK0(Spatialize, Source, get3DSpatialize, const)
DECL_THUNK0(ALsizei, Source, getResamplerIndex, const)
DECL_THUNK0(ALfloat, Source, getAirAbsorptionFactor, const)
DECL_THUNK0(BoolTriple, Source, getGainAuto, const)

}
