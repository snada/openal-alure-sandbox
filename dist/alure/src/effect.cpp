
#include "config.h"

#include "effect.h"

#include <stdexcept>

#include "context.h"

namespace alure
{

template<typename T>
static inline T clamp(const T& val, const T& min, const T& max)
{ return std::min<T>(std::max<T>(val, min), max); }

DECL_THUNK1(void, Effect, setReverbProperties,, const EFXEAXREVERBPROPERTIES&)
void EffectImpl::setReverbProperties(const EFXEAXREVERBPROPERTIES &props)
{
    CheckContext(mContext);

    if(mType != AL_EFFECT_EAXREVERB && mType != AL_EFFECT_REVERB)
    {
        alGetError();
        mContext->alEffecti(mId, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
        if(alGetError() == AL_NO_ERROR)
            mType = AL_EFFECT_EAXREVERB;
        else
        {
            mContext->alEffecti(mId, AL_EFFECT_TYPE, AL_EFFECT_REVERB);
            if(alGetError() != AL_NO_ERROR)
                throw std::runtime_error("Failed to set reverb type");
            mType = AL_EFFECT_REVERB;
        }
    }

    if(mType == AL_EFFECT_EAXREVERB)
    {
#define SETPARAM(e,t,v) mContext->alEffectf((e), AL_EAXREVERB_##t, clamp((v), AL_EAXREVERB_MIN_##t, AL_EAXREVERB_MAX_##t))
        SETPARAM(mId, DENSITY, props.flDensity);
        SETPARAM(mId, DIFFUSION, props.flDiffusion);
        SETPARAM(mId, GAIN, props.flGain);
        SETPARAM(mId, GAINHF, props.flGainHF);
        SETPARAM(mId, GAINLF, props.flGainLF);
        SETPARAM(mId, DECAY_TIME, props.flDecayTime);
        SETPARAM(mId, DECAY_HFRATIO, props.flDecayHFRatio);
        SETPARAM(mId, DECAY_LFRATIO, props.flDecayLFRatio);
        SETPARAM(mId, REFLECTIONS_GAIN, props.flReflectionsGain);
        SETPARAM(mId, REFLECTIONS_DELAY, props.flReflectionsDelay);
        mContext->alEffectfv(mId, AL_EAXREVERB_REFLECTIONS_PAN, props.flReflectionsPan);
        SETPARAM(mId, LATE_REVERB_GAIN, props.flLateReverbGain);
        SETPARAM(mId, LATE_REVERB_DELAY, props.flLateReverbDelay);
        mContext->alEffectfv(mId, AL_EAXREVERB_LATE_REVERB_PAN, props.flLateReverbPan);
        SETPARAM(mId, ECHO_TIME, props.flEchoTime);
        SETPARAM(mId, ECHO_DEPTH, props.flEchoDepth);
        SETPARAM(mId, MODULATION_TIME, props.flModulationTime);
        SETPARAM(mId, MODULATION_DEPTH, props.flModulationDepth);
        SETPARAM(mId, AIR_ABSORPTION_GAINHF, props.flAirAbsorptionGainHF);
        SETPARAM(mId, HFREFERENCE, props.flHFReference);
        SETPARAM(mId, LFREFERENCE, props.flLFReference);
        SETPARAM(mId, ROOM_ROLLOFF_FACTOR, props.flRoomRolloffFactor);
        mContext->alEffecti(mId, AL_EAXREVERB_DECAY_HFLIMIT, (props.iDecayHFLimit ? AL_TRUE : AL_FALSE));
#undef SETPARAM
    }
    else if(mType == AL_EFFECT_REVERB)
    {
#define SETPARAM(e,t,v) mContext->alEffectf((e), AL_REVERB_##t, clamp((v), AL_REVERB_MIN_##t, AL_REVERB_MAX_##t))
        SETPARAM(mId, DENSITY, props.flDensity);
        SETPARAM(mId, DIFFUSION, props.flDiffusion);
        SETPARAM(mId, GAIN, props.flGain);
        SETPARAM(mId, GAINHF, props.flGainHF);
        SETPARAM(mId, DECAY_TIME, props.flDecayTime);
        SETPARAM(mId, DECAY_HFRATIO, props.flDecayHFRatio);
        SETPARAM(mId, REFLECTIONS_GAIN, props.flReflectionsGain);
        SETPARAM(mId, REFLECTIONS_DELAY, props.flReflectionsDelay);
        SETPARAM(mId, LATE_REVERB_GAIN, props.flLateReverbGain);
        SETPARAM(mId, LATE_REVERB_DELAY, props.flLateReverbDelay);
        SETPARAM(mId, AIR_ABSORPTION_GAINHF, props.flAirAbsorptionGainHF);
        SETPARAM(mId, ROOM_ROLLOFF_FACTOR, props.flRoomRolloffFactor);
        mContext->alEffecti(mId, AL_REVERB_DECAY_HFLIMIT, (props.iDecayHFLimit ? AL_TRUE : AL_FALSE));
#undef SETPARAM
    }
}

void Effect::destroy()
{
    EffectImpl *i = pImpl;
    pImpl = nullptr;
    i->destroy();
}
void EffectImpl::destroy()
{
    CheckContext(mContext);

    alGetError();
    mContext->alDeleteEffects(1, &mId);
    if(alGetError() != AL_NO_ERROR)
        throw std::runtime_error("Effect failed to delete");
    mId = 0;

    delete this;
}

}
