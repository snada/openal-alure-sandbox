#ifndef SOURCEGROUP_H
#define SOURCEGROUP_H

#include "main.h"


namespace alure
{

struct SourceGroupProps {
    ALfloat mGain;
    ALfloat mPitch;

    SourceGroupProps() : mGain(1.0f), mPitch(1.0f) { }
};

class SourceGroupImpl : SourceGroupProps {
    ContextImpl *const mContext;

    Vector<SourceImpl*> mSources;
    Vector<SourceGroupImpl*> mSubGroups;

    SourceGroupProps mParentProps;
    SourceGroupImpl *mParent;

    const String mName;

    void update(ALfloat gain, ALfloat pitch);

    void unsetParent();

    void insertSubGroup(SourceGroupImpl *group);
    void eraseSubGroup(SourceGroupImpl *group);

    bool findInSubGroups(SourceGroupImpl *group) const;

    void collectPlayingSourceIds(Vector<ALuint> &sourceids) const;
    void updatePausedStatus() const;

    void collectPausedSourceIds(Vector<ALuint> &sourceids) const;
    void updatePlayingStatus() const;

    void collectSourceIds(Vector<ALuint> &sourceids) const;
    void updateStoppedStatus() const;

public:
    SourceGroupImpl(ContextImpl *context, StringView name)
      : mContext(context), mParent(nullptr), mName(String(name))
    { }

    ALfloat getAppliedGain() const { return mGain * mParentProps.mGain; }
    ALfloat getAppliedPitch() const { return mPitch * mParentProps.mPitch; }

    void insertSource(SourceImpl *source);
    void eraseSource(SourceImpl *source);

    void setParentGroup(SourceGroup group);
    SourceGroup getParentGroup() const { return SourceGroup(mParent); }

    Vector<Source> getSources() const;

    Vector<SourceGroup> getSubGroups() const;

    void setGain(ALfloat gain);
    ALfloat getGain() const { return mGain; }

    void setPitch(ALfloat pitch);
    ALfloat getPitch() const { return mPitch; }

    void pauseAll() const;
    void resumeAll() const;

    void stopAll() const;

    StringView getName() const { return mName; }

    void release();
};

} // namespace alure2

#endif /* SOURCEGROUP_H */
