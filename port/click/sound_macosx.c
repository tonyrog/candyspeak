
#include <Carbon/Carbon.h>
#include <AvailabilityMacros.h>
#include <Foundation/Foundation.h>
#include <CoreServices/CoreServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h> //for file stuff
#include <CoreAudio/AudioHardware.h>
#include <AudioUnit/AudioUnit.h>
#include <memory.h>

#include "sound.h"

static IOReturn snd_err;

/* pretty print error */
static void print_error(FILE* f, IOReturn err)
{
    switch(err) {
    case kIOReturnSuccess:
	fprintf(f, "OK\n");
	break;
    case kIOReturnError:
	fprintf(f, "general error\n");
	break;
    case kIOReturnNoMemory:
	fprintf(f, "can't allocate memory\n");
	break;
    case kIOReturnNoResources:
	fprintf(f, "resource shortage \n");
	break;
    case kIOReturnIPCError:
	fprintf(f, "error during IPC \n");
	break;
    case kIOReturnNoDevice:
	fprintf(f, "no such device \n");
	break;
    case kIOReturnNotPrivileged:
	fprintf(f, "privilege violation \n");
	break;
    case kIOReturnBadArgument:
	fprintf(f, "invalid argument \n");
	break;
    case kIOReturnLockedRead:
	fprintf(f, "device read locked \n");
	break;
    case kIOReturnLockedWrite:
	fprintf(f, "device write locked \n");
	break;
    case kIOReturnExclusiveAccess:
	fprintf(f, "exclusive access and device already open\n");
	break;
    case kIOReturnBadMessageID:
	fprintf(f, "sent/received messages had different msg_id\n");
	break;
    case kIOReturnUnsupported:
	fprintf(f, "unsupported function\n");
	break;
    case kIOReturnVMError:
	fprintf(f, "misc. VM failure \n");
	break;
    case kIOReturnInternalError:
	fprintf(f, "internal error \n");
	break;
    case kIOReturnIOError:
	fprintf(f, "General I/O error \n");
	break;
    case kIOReturnCannotLock:
	fprintf(f, "can't acquire lock\n");
	break;
    case kIOReturnNotOpen:
	fprintf(f, "device not open \n");
	break;
    case kIOReturnNotReadable:
	fprintf(f, "read not supported \n");
	break;
    case kIOReturnNotWritable:
	fprintf(f, "write not supported\n");
	break;
    case kIOReturnNotAligned:
	fprintf(f, "alignment error\n");
	break;
    case kIOReturnBadMedia:
	fprintf(f, "Media Error\n");
	break;
    case kIOReturnStillOpen:
	fprintf(f, "device(s) still open\n");
	break;
    case kIOReturnRLDError:
	fprintf(f, "rld failure\n");
	break;
    case kIOReturnDMAError:
	fprintf(f, "DMA failure\n");
	break;
    case kIOReturnBusy:
	fprintf(f, "Device Busy\n");
	break;
    case kIOReturnTimeout:
	fprintf(f, "I/O Timeout\n");
	break;
    case kIOReturnOffline:
	fprintf(f, "device offline\n");
	break;
    case kIOReturnNotReady:
	fprintf(f, "not ready\n");
	break;
    case kIOReturnNotAttached:
	fprintf(f, "device not attached\n");
	break;
    case kIOReturnNoChannels:
	fprintf(f, "no DMA channels left\n");
	break;
    case kIOReturnNoSpace:
	fprintf(f, "no space for data\n");
	break;
    case kIOReturnPortExists:
	fprintf(f, "port already exists\n");
	break;
    case kIOReturnCannotWire:
	fprintf(f, "can't wire down physical memory\n");
	break;
    case kIOReturnNoInterrupt:
	fprintf(f, "no interrupt attached\n");
	break;
    case kIOReturnNoFrames:
	fprintf(f, "no DMA frames enqueued\n");
	break;
    case kIOReturnMessageTooLarge:
	fprintf(f, "oversized msg received\n");
	break;
    case kIOReturnNotPermitted:
	fprintf(f, "not permitted\n");
	break;
    case kIOReturnNoPower:
	fprintf(f, "no power to device\n");
	break;
    case kIOReturnNoMedia:
	fprintf(f, "media not present\n");
	break;
    case kIOReturnUnformattedMedia:
	fprintf(f, "media not formatted\n");
	break;
    case kIOReturnUnsupportedMode:
	fprintf(f, "no such mode\n");
	break;
    case kIOReturnUnderrun:
	fprintf(f, "data underrun\n");
	break;
    case kIOReturnOverrun:
	fprintf(f, "data overrun\n");
	break;
    case kIOReturnDeviceError:
	fprintf(f, "the device is not working properly!\n");
	break;
    case kIOReturnNoCompletion:
	fprintf(f, "a completion routine is required\n");
	break;
     case kIOReturnAborted:
	 fprintf(f, "operation aborted\n");
	break;
    case kIOReturnNoBandwidth:
	fprintf(f, "bus bandwidth would be exceeded\n");
	break;
    case kIOReturnNotResponding:
	fprintf(f, "device not responding\n");
	break;
    case kIOReturnIsoTooOld:
	fprintf(f, "isochronous I/O request for distant past!\n");
	break;
    case kIOReturnIsoTooNew:
	fprintf(f, "isochronous I/O request for distant future\n");
	break;
    case kIOReturnNotFound:
	fprintf(f, "data was not found\n");
	break;
    case kIOReturnInvalid:
	fprintf(f, "should never be seen\n");
	break;
	/* Add HCI error codes */
    default:
	fprintf(f, "ERROR: code=%d, sub=%d, system=%d\n",
		err_get_code(err), 
		err_get_sub(err), 
		err_get_system(err));
	break;
    }
}

void sound_device_print_last_error(FILE* f)
{
    print_error(f, snd_err);
}



static int match_device_formats(AudioDeviceID device,
				Boolean isInput,
				char* match_name,
				AudioStreamBasicDescription* match,
				AudioStreamBasicDescription* result)
{
    OSStatus err = noErr;
    AudioStreamBasicDescription format[128];
    char buf[256];
    // UInt64 dspFormat = 0LL;
    uint32_t count;
    uint32_t i;
    // uint32_t n;

    if (match_name) {
	count = sizeof(buf);
	err = AudioDeviceGetProperty(
	    device, 0, false,
	    kAudioDevicePropertyDeviceName,
	    &count, buf);
	if (err != kIOReturnSuccess)
	    return 0;
	printf("Match: [%s] with [%s]\n", buf, match_name);
	if (strcasestr(buf, match_name) == NULL)
	    return 0;
    }

    count = sizeof(format);

    err = AudioDeviceGetProperty(
	device, 0, isInput,
	kAudioDevicePropertyStreamFormats,
	&count, &format[0]);
    if (err != kIOReturnSuccess)
	return 0;
    printf("Device: %u\n", (unsigned int) device);

    for (i = 0; i < count/sizeof(AudioStreamBasicDescription); i++) {
	if (match->mBitsPerChannel &&
	    (format[i].mBitsPerChannel < match->mBitsPerChannel))
	    continue;
	if (match->mChannelsPerFrame &&
	    (format[i].mChannelsPerFrame < match->mChannelsPerFrame))
	    continue;
	if ((match->mSampleRate > 0.0) &&
	    (format[i].mSampleRate < match->mSampleRate))
	    continue;
	*result = format[i];
	return 1;
    }
    return 0;
}


static AudioDeviceID find_device(AudioStreamBasicDescription* match,
				 Boolean isInput,
				 char* match_name,
				 AudioStreamBasicDescription* result)
{
    AudioDeviceID dev[128];
    uint32_t count;
    int i;
    AudioStreamBasicDescription format;
    AudioStreamBasicDescription selectedFormat;
    int selected = -1;

    count = sizeof(dev);
    snd_err = AudioHardwareGetProperty(kAudioHardwarePropertyDevices,
				       &count, (void*)dev);
    if (snd_err != kIOReturnSuccess)
	return 0;

    printf("Number of devices: %lu\n", (count/sizeof(AudioDeviceID)));
    for (i = 0; i < (count/sizeof(AudioDeviceID)); i++) {
	if (match_device_formats(dev[i], isInput, match_name, match, &format)) {
	    if (selected < 0) {
		selectedFormat = format;
		selected = i;
	    }
	}
    }
    if (selected < 0)
	return 0;
    *result = selectedFormat;
    return dev[selected];
}


int sound_device_name(sound_device_t* device, char* buf, uint32_t maxlen)
{
    snd_err = AudioDeviceGetProperty((AudioDeviceID)device, 0, false,
				     kAudioDevicePropertyDeviceName,
				     &maxlen, buf);
    if (snd_err != noErr)
	return -1;
    return 0;
}

int sound_device_manuf(sound_device_t* device, char* buf, uint32_t maxlen)
{
    snd_err = AudioDeviceGetProperty((AudioDeviceID)device, 0, false,
				     kAudioDevicePropertyDeviceManufacturer,
				     &maxlen, buf);
    if (snd_err != noErr)
	return -1;
    return 0;
}

int sound_device_buffer_size(sound_device_t* device, sound_dir_t dir)
{
    uint32_t bufferSize;
    uint32_t count = sizeof(bufferSize);
    snd_err = AudioDeviceGetProperty((AudioDeviceID)device, 0,
				     (dir & SOUND_DIR_INPUT) != 0,
				     kAudioDevicePropertyBufferSize,
				     &count, &bufferSize);
    if (snd_err != noErr)
	return -1;
    return (int) bufferSize;
}


sound_device_t* sound_device_open(sound_dir_t dir, sound_format_t* format)
{
    AudioDeviceID device;
    AudioStreamBasicDescription match;
    AudioStreamBasicDescription selected;
    Boolean isInput = (dir & SOUND_DIR_INPUT) != 0;

    // convert format into sound_format_t into AudioStreamBasicDescription
    memset(&match, 0, sizeof(match));
    match.mBytesPerPacket = format->bytes_per_packet;
    match.mFramesPerPacket = format->frames_per_packet;
    match.mBytesPerFrame = format->bytes_per_frame;
    match.mChannelsPerFrame = format->channels_per_frame;
    match.mBitsPerChannel = format->bits_per_channel;
    match.mSampleRate = format->sample_rate;
    if (format->flags & SOUND_FORMAT_BIG_ENDIAN)
	match.mFormatFlags |= kAudioFormatFlagIsBigEndian;
    if (format->flags & SOUND_FORMAT_FLOAT)
	match.mFormatFlags |= kAudioFormatFlagIsFloat;
    if (format->flags & SOUND_FORMAT_SIGNED)
	match.mFormatFlags |= kAudioFormatFlagIsSignedInteger;

    if ((device = find_device(&match,isInput,format->name,&selected)) > 0) {
	uint32_t bufferSize;
	uint32_t count = sizeof(bufferSize);
	snd_err = AudioDeviceGetProperty(device, 0, isInput,
					 kAudioDevicePropertyBufferSize, 
					 &count, &bufferSize);
	if (snd_err != kAudioHardwareNoError)
	    return NULL;

	// First set property
	count = sizeof(AudioStreamBasicDescription);
	snd_err = AudioDeviceSetProperty(device, 0, 0, isInput,
					 kAudioDevicePropertyStreamFormat,
					 count, &selected);
	if (snd_err != kAudioHardwareNoError)
	    return NULL;

	/* Then read it again */
	count = sizeof(AudioStreamBasicDescription);
	snd_err = AudioDeviceGetProperty(device, 0, isInput,
				     kAudioDevicePropertyStreamFormat,
				     &count, &selected);

	if (snd_err != kAudioHardwareNoError)
	    return NULL;

	//  AudioStreamBasicDescription => sound_format_t
	format->bytes_per_packet = selected.mBytesPerPacket;
	format->frames_per_packet = selected.mFramesPerPacket;
	format->bytes_per_frame = selected.mBytesPerFrame;
	format->channels_per_frame = selected.mChannelsPerFrame;
	format->bits_per_channel = selected.mBitsPerChannel;
	format->sample_rate = selected.mSampleRate;
	format->flags = 0;
	if (selected.mFormatFlags & kAudioFormatFlagIsBigEndian)
	    format->flags |= SOUND_FORMAT_BIG_ENDIAN;
	if (selected.mFormatFlags & kAudioFormatFlagIsFloat)
	    format->flags |= SOUND_FORMAT_FLOAT;
	if (selected.mFormatFlags & kAudioFormatFlagIsSignedInteger)
	    format->flags |= SOUND_FORMAT_SIGNED;
    }
    return device;
}

/* The I/O audio proc */
OSStatus audio_proc(AudioDeviceID  inDevice,
		    const AudioTimeStamp*  inNow, 
		    const AudioBufferList* inInputData, 
		    const AudioTimeStamp*  inInputTime, 
		    AudioBufferList*  outOutputData, 
		    const AudioTimeStamp* inOutputTime,
		    void* appData)
{
    sound_thread_t* td = (sound_thread_t*) appData;
    void* data = td->data;

    if ((td->dir & SOUND_DIR_INPUT) && (td->sound_input != NULL))
	(*td->sound_input)(inNow, inInputData, inInputTime, data);
    if ((td->dir & SOUND_DIR_OUTPUT) && (td->sound_output != NULL))
	(*td->sound_output)(inNow, outOutputData, inOutputTime, data);
    return kAudioHardwareNoError;
}


int sound_main(sound_thread_t* td)
{
    IOReturn err;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

    printf("enter sound_main\n");

    err = AudioDeviceAddIOProc(td->device, (AudioDeviceIOProc)audio_proc,
			       (void*) td);
    if (err != kAudioHardwareNoError) {
	print_error(stderr, err);
	return -1;
    }
    err = AudioDeviceStart(td->device, (AudioDeviceIOProc)audio_proc);
    if (err != kAudioHardwareNoError) {
	print_error(stderr, err);
	return -1;
    }
    td->running = 1;
    CFRunLoopRun();

    [pool release];
    return 0;
}
