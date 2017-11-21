
#include "config.h"

#include "devicemanager.h"
#include "device.h"
#include "main.h"

#include <algorithm>
#include <stdexcept>
#include <iostream>

#include "alc.h"
#include "al.h"

namespace alure
{

std::string alc_category::message(int condition) const
{
    switch(condition)
    {
    case ALC_NO_ERROR: return "No error";
    case ALC_INVALID_ENUM: return "Invalid enum";
    case ALC_INVALID_VALUE: return "Invalid value";
    case ALC_INVALID_DEVICE: return "Invalid device";
    case ALC_INVALID_CONTEXT: return "Invalid context";
    case ALC_OUT_OF_MEMORY: return "Out of memory";
    }
    return "Unknown ALC error "+std::to_string(condition);
}

std::string al_category::message(int condition) const
{
    switch(condition)
    {
    case AL_NO_ERROR: return "No error";
    case AL_INVALID_NAME: return "Invalid name";
    case AL_INVALID_ENUM: return "Invalid enum";
    case AL_INVALID_VALUE: return "Invalid value";
    case AL_INVALID_OPERATION: return "Invalid operation";
    case AL_OUT_OF_MEMORY: return "Out of memory";
    }
    return "Unknown AL error "+std::to_string(condition);
}

alc_category alc_category::sSingleton;
al_category al_category::sSingleton;


template<typename T>
static inline void GetDeviceProc(T *&func, ALCdevice *device, const char *name)
{ func = reinterpret_cast<T*>(alcGetProcAddress(device, name)); }


ALCboolean (ALC_APIENTRY*DeviceManagerImpl::SetThreadContext)(ALCcontext*);

DeviceManager DeviceManager::get()
{ return DeviceManager(DeviceManagerImpl::get()); }
DeviceManagerImpl &DeviceManagerImpl::get()
{
    static DeviceManagerImpl singleton;
    return singleton;
}


DeviceManagerImpl::DeviceManagerImpl()
{
    if(alcIsExtensionPresent(0, "ALC_EXT_thread_local_context"))
        GetDeviceProc(SetThreadContext, nullptr, "alcSetThreadContext");
}

DeviceManagerImpl::~DeviceManagerImpl()
{
}


bool DeviceManager::queryExtension(const String &name) const
{ return pImpl.queryExtension(name.c_str()); }
bool DeviceManager::queryExtension(const char *name) const
{ return pImpl.queryExtension(name); }
bool DeviceManagerImpl::queryExtension(const char *name) const
{
    return alcIsExtensionPresent(nullptr, name);
}

Vector<String> DeviceManager::enumerate(DeviceEnumeration type) const
{ return pImpl.enumerate(type); }
Vector<String> DeviceManagerImpl::enumerate(DeviceEnumeration type) const
{
    Vector<String> list;
    if(type == DeviceEnumeration::Full && !alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
        type = DeviceEnumeration::Basic;
    const ALCchar *names = alcGetString(nullptr, (ALenum)type);
    while(names && *names)
    {
        list.emplace_back(names);
        names += strlen(names)+1;
    }
    return list;
}

String DeviceManager::defaultDeviceName(DefaultDeviceType type) const
{ return pImpl.defaultDeviceName(type); }
String DeviceManagerImpl::defaultDeviceName(DefaultDeviceType type) const
{
    if(type == DefaultDeviceType::Full && !alcIsExtensionPresent(nullptr, "ALC_ENUMERATE_ALL_EXT"))
        type = DefaultDeviceType::Basic;
    const ALCchar *name = alcGetString(nullptr, (ALenum)type);
    return name ? String(name) : String();
}


Device DeviceManager::openPlayback(const String &name)
{ return pImpl.openPlayback(name.c_str()); }
Device DeviceManager::openPlayback(const char *name)
{ return pImpl.openPlayback(name); }
Device DeviceManagerImpl::openPlayback(const char *name)
{
    ALCdevice *dev = alcOpenDevice(name);
    if(!dev) throw alc_error(alcGetError(nullptr), "alcOpenDevice failed");

    try {
        mDevices.emplace_back(MakeUnique<DeviceImpl>(dev));
        return Device(mDevices.back().get());
    }
    catch(...) {
        alcCloseDevice(dev);
        throw;
    }
}

Device DeviceManager::openPlayback(const String &name, const std::nothrow_t &nt) noexcept
{ return pImpl.openPlayback(name.c_str(), nt); }
Device DeviceManager::openPlayback(const std::nothrow_t&) noexcept
{ return pImpl.openPlayback(nullptr, std::nothrow); }
Device DeviceManager::openPlayback(const char *name, const std::nothrow_t &nt) noexcept
{ return pImpl.openPlayback(name, nt); }
Device DeviceManagerImpl::openPlayback(const char *name, const std::nothrow_t&) noexcept
{
    try {
        return openPlayback(name);
    }
    catch(...) {
    }
    return Device();
}

void DeviceManagerImpl::removeDevice(DeviceImpl *dev)
{
    auto iter = std::find_if(mDevices.begin(), mDevices.end(),
        [dev](const UniquePtr<DeviceImpl> &entry) -> bool
        { return entry.get() == dev; }
    );
    if(iter != mDevices.end()) mDevices.erase(iter);
}

}
