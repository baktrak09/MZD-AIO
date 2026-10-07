#include "audio.h"
#include <stdarg.h>
#include <time.h>

static const char* AA_DIAG_PATH = "/tmp/aa-audio-diag.log";

void aa_diag_log(const char* fmt, ...)
{
    FILE* f = fopen(AA_DIAG_PATH, "a");
    if (!f) return;
    time_t now = time(NULL);
    fprintf(f, "[AA_DIAG %ld] ", (long)now);
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fprintf(f, "\n");
    fclose(f);
}

AudioOutput::AudioOutput(const char *outDev, const char *voiceDev)
{
    FILE* diag = fopen(AA_DIAG_PATH, "w");
    if (diag) { fprintf(diag, "=== Android Auto audio diagnostic log ===\n"); fclose(diag); }
    const char* speechDev = voiceDev ? voiceDev : outDev;
    aa_diag_log("AudioOutput ctor media=%s speech=%s ALSA=%s", outDev, speechDev, snd_asoundlib_version());
    printf("snd_asoundlib_version: %s\n", snd_asoundlib_version());
    logd("Device name %s\n", outDev);
    int err = 0;
    if ((err = snd_pcm_open(&aud_handle, outDev, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        loge("Playback open error: %s\n", snd_strerror(err));
    }
    if ((err = snd_pcm_set_params(aud_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 2,48000, 1, 1000000)) < 0) {   /* 1.0sec */
        loge("Playback open error: %s\n", snd_strerror(err));
    }

    if ((err = snd_pcm_prepare(aud_handle)) < 0) {
        loge("snd_pcm_prepare error: %s\n", snd_strerror(err));
    }

    if ((err = snd_pcm_open(&au1_handle, speechDev, SND_PCM_STREAM_PLAYBACK, 0)) < 0) {
        loge("Playback open error: %s\n", snd_strerror(err));
    }
    if ((err = snd_pcm_set_params(au1_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1, 16000, 1, 1000000)) < 0) {   /* 1.0sec */
        loge("Playback open error: %s\n", snd_strerror(err));
    }

    if ((err = snd_pcm_prepare(au1_handle)) < 0) {
        loge("snd_pcm_prepare error: %s\n", snd_strerror(err));
    }
}

AudioOutput::~AudioOutput()
{
    snd_pcm_close(aud_handle);
    snd_pcm_close(au1_handle);
}

void AudioOutput::MediaPacketAUD(uint64_t timestamp, const byte *buf, int len)
{
    audPacketCount++;
    audByteCount += len;
    if (audPacketCount == 1 || (audPacketCount % 100) == 0) {
        aa_diag_log("AUD RX packets=%llu bytes=%llu len=%d ts=%llu",
                    (unsigned long long)audPacketCount, (unsigned long long)audByteCount,
                    len, (unsigned long long)timestamp);

        // Inspect the actual S16_LE media samples. Successful ALSA writes can still
        // contain digital silence, so record peak amplitude and non-zero sample count.
        unsigned int sampleCount = (len > 1) ? (unsigned int)(len / 2) : 0;
        unsigned int nonZeroSamples = 0;
        unsigned int peakAbs = 0;
        unsigned long long sumAbs = 0;
        for (unsigned int i = 0; i < sampleCount; ++i) {
            unsigned int lo = (unsigned int)buf[i * 2];
            unsigned int hi = (unsigned int)buf[i * 2 + 1];
            int sample = (int)(lo | (hi << 8));
            if (sample & 0x8000)
                sample -= 0x10000;
            unsigned int magnitude = (sample < 0) ? (unsigned int)(-sample) : (unsigned int)sample;
            if (magnitude != 0)
                ++nonZeroSamples;
            if (magnitude > peakAbs)
                peakAbs = magnitude;
            sumAbs += magnitude;
        }
        unsigned int avgAbs = sampleCount ? (unsigned int)(sumAbs / sampleCount) : 0;
        aa_diag_log("AUD CONTENT packets=%llu samples=%u nonzero=%u peak=%u avgabs=%u",
                    (unsigned long long)audPacketCount, sampleCount, nonZeroSamples, peakAbs, avgAbs);
    }

    if (aud_handle)
        MediaPacket(aud_handle, buf, len, "AUD");
    else if (audPacketCount == 1)
        aa_diag_log("AUD RX but handle NULL");
}

void AudioOutput::MediaPacketAU1(uint64_t timestamp, const byte *buf, int len)
{
    au1PacketCount++;
    au1ByteCount += len;
    if (au1PacketCount == 1 || (au1PacketCount % 100) == 0)
        aa_diag_log("AU1 RX packets=%llu bytes=%llu len=%d ts=%llu",
                    (unsigned long long)au1PacketCount, (unsigned long long)au1ByteCount,
                    len, (unsigned long long)timestamp);

    if (au1_handle)
        MediaPacket(au1_handle, buf, len, "AU1");
    else if (au1PacketCount == 1)
        aa_diag_log("AU1 RX but handle NULL");
}

snd_pcm_sframes_t AudioOutput::MediaPacket(snd_pcm_t *pcm, const byte *buf, int len, const char* streamName)
{
    snd_pcm_sframes_t framecount = snd_pcm_bytes_to_frames(pcm, len);
    snd_pcm_sframes_t frames = snd_pcm_writei(pcm, buf, framecount);

    if (frames < 0) {
        aa_diag_log("%s write FAILED requested=%ld result=%ld error=%s state=%s",
                    streamName, (long)framecount, (long)frames, snd_strerror(frames),
                    snd_pcm_state_name(snd_pcm_state(pcm)));

        frames = snd_pcm_recover(pcm, frames, 1);
        if (frames < 0) {
            loge("snd_pcm_recover failed: %s\n", snd_strerror(frames));
            aa_diag_log("%s recover FAILED result=%ld error=%s",
                        streamName, (long)frames, snd_strerror(frames));
        } else {
            frames = snd_pcm_writei(pcm, buf, framecount);
            aa_diag_log("%s retry write result=%ld requested=%ld",
                        streamName, (long)frames, (long)framecount);
        }
    }

    if (frames >= 0 && frames < framecount) {
        loge("Short write (expected %i, wrote %i)\n", (int)framecount, (int)frames);
        aa_diag_log("%s SHORT WRITE requested=%ld wrote=%ld",
                    streamName, (long)framecount, (long)frames);
    }

    uint64_t packetCount = (streamName[2] == 'D') ? audPacketCount : au1PacketCount;
    if (packetCount == 1 || (packetCount % 100) == 0) {
        snd_pcm_sframes_t delay = 0;
        snd_pcm_sframes_t avail = snd_pcm_avail_update(pcm);
        int delayResult = snd_pcm_delay(pcm, &delay);
        aa_diag_log("%s PCM packets=%llu requested=%ld wrote=%ld state=%s avail=%ld delay=%ld delay_rc=%d",
                    streamName,
                    (unsigned long long)packetCount,
                    (long)framecount,
                    (long)frames,
                    snd_pcm_state_name(snd_pcm_state(pcm)),
                    (long)avail,
                    (long)delay,
                    delayResult);
    }

    return frames;
}

snd_pcm_sframes_t MicInput::read_mic_cancelable(snd_pcm_t* mic_handle, void *buffer, snd_pcm_uframes_t size, bool* canceled)
{
    int pollfdAllocCount = snd_pcm_poll_descriptors_count(mic_handle);
    struct pollfd* pfds = (struct pollfd*)alloca((pollfdAllocCount + 1) * sizeof(struct pollfd));
    unsigned short* revents = (unsigned short*)alloca(pollfdAllocCount * sizeof(unsigned short));

    int polldescs = snd_pcm_poll_descriptors(mic_handle, pfds, pollfdAllocCount);
    pfds[polldescs].fd = cancelPipeRead;
    pfds[polldescs].events = POLLIN;
    pfds[polldescs].revents = 0;

    *canceled = false;
    while (true)
    {
        if (poll(pfds, polldescs+1, -1) <= 0)
        {
            loge("poll failed");
            break;
        }

        if (pfds[polldescs].revents & POLLIN)
        {
            unsigned char bogusByte;
            read(cancelPipeRead, &bogusByte, 1);

            *canceled = true;
            return 0;
        }
        unsigned short audioEvents = 0;
        snd_pcm_poll_descriptors_revents(mic_handle, pfds, polldescs, &audioEvents);

        if (audioEvents & POLLIN)
        {
            //got it
            break;
        }
    }
    snd_pcm_sframes_t ret = snd_pcm_readi(mic_handle, buffer, size);
    return ret;
}

void MicInput::MicThreadMain(IHUAnyThreadInterface* threadInterface)
{
    pthread_setname_np(pthread_self(), "mic_thread");

    snd_pcm_t* mic_handle = nullptr;

    int err = 0;
    if ((err = snd_pcm_open(&mic_handle, micDevice.c_str(), SND_PCM_STREAM_CAPTURE,  SND_PCM_NONBLOCK)) < 0)
    {
        loge("Playback open error: %s\n", snd_strerror(err));
        return;
    }

    if ((err = snd_pcm_set_params(mic_handle, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,16000, 1, 250000)) < 0)
    {   /* 0.25sec */
        loge("Playback open error: %s\n", snd_strerror(err));
        snd_pcm_close(mic_handle);
        return;
    }
    if ((err = snd_pcm_prepare(mic_handle)) < 0)
    {
        loge("snd_pcm_prepare: %s\n", snd_strerror(err));
        snd_pcm_close(mic_handle);
        return;
    }

    if ((err = snd_pcm_start(mic_handle)) < 0)
    {
        loge("snd_pcm_start: %s\n", snd_strerror(err));
        snd_pcm_close(mic_handle);
        return;
    }

    const size_t tempSize = 1024*1024;
    const snd_pcm_sframes_t bufferFrameCount = snd_pcm_bytes_to_frames(mic_handle, tempSize);
    bool canceled = false;
    while(!canceled)
    {
        uint8_t* tempBuffer = new uint8_t[tempSize];
        snd_pcm_sframes_t frames = read_mic_cancelable(mic_handle, tempBuffer, bufferFrameCount, &canceled);
        if (frames < 0)
        {
            if (frames == -ESTRPIPE)
            {
                frames = snd_pcm_recover(mic_handle, frames, 0);
                if (frames < 0)
                {
                    loge("recover failed");
                }
                else
                {
                    frames = read_mic_cancelable(mic_handle, tempBuffer, bufferFrameCount, &canceled);
                }
            }

            if (frames < 0)
            {
                delete [] tempBuffer;
                canceled = true;
            }
        }
        ssize_t bytesRead = snd_pcm_frames_to_bytes(mic_handle, frames);
        threadInterface->hu_queue_command([tempBuffer, bytesRead](IHUConnectionThreadInterface& s)
        {
            //doesn't seem like the timestamp is used so pass 0
            s.hu_aap_enc_send_media_packet(1, AA_CH_MIC, HU_PROTOCOL_MESSAGE::MediaDataWithTimestamp, 0, tempBuffer, bytesRead);
            delete [] tempBuffer;
        });
    }

    if ((err = snd_pcm_drop(mic_handle)) < 0)
    {
        loge("snd_pcm_drop: %s\n", snd_strerror(err));
    }

    snd_pcm_close(mic_handle);
}

MicInput::MicInput(const std::string& micDevice) : micDevice(micDevice)
{
    int cancelPipe[2];
    if (pipe(cancelPipe) < 0)
    {
        loge("pipe failed");
    }
    cancelPipeRead = cancelPipe[0];
    cancelPipeWrite = cancelPipe[1];
}

MicInput::~MicInput()
{
    Stop();

    close(cancelPipeRead);
    close(cancelPipeWrite);
}

void MicInput::Start(IHUAnyThreadInterface* threadInterface)
{
    if (!mic_readthread.joinable())
    {
        mic_readthread = std::thread([this, threadInterface](){ MicThreadMain(threadInterface);});
    }
}

void MicInput::Stop()
{
    if (mic_readthread.joinable())
    {
        //write single byte
        write(cancelPipeWrite, &cancelPipeWrite, 1);
        mic_readthread.join();
    }
}
