#include <vector>
#include <queue>
#include <memory>

#include <stdio.h>
#include <unistd.h>

#include <openal/al.h>
#include <openal/alc.h>
#include <pthread.h>

#define CHECK_AL_ERRORS(func) \
    { \
        ALenum error = alGetError(); \
        if (error != AL_NO_ERROR) { \
            printf("%s: %s", func, alGetString(error)); \
            return false; \
        } \
    }

#define CHECK_AL_ERRORS_AND_IGNORE(func) \
    { \
        ALenum error = alGetError(); \
        if (error != AL_NO_ERROR) { \
            printf("%s: %s", func, alGetString(error)); \
        } \
    }

#define CHECK_ALC_ERRORS(func) \
    { \
        ALCenum error = alcGetError(device); \
        if (error != AL_NO_ERROR) { \
            printf("%s: %s", func, alcGetString(device, error)); \
            return false; \
        } \
    }

#define CHECK_ALC_ERRORS_AND_IGNORE(func) \
    { \
        ALCenum error = alcGetError(device); \
        if (error != AL_NO_ERROR) { \
            printf("%s: %s", func, alcGetString(device, error)); \
        } \
    }

struct AudioFrame {
    int sampleCount;
    int sampleRate;
    int channelCount;

    std::vector<int16_t> samples;
};

class AudioRenderer {
    private:
        static const int numBuffers = 5;

        std::queue<AudioFrame *> audioQueue;

        int queuedSampleCount;
        int bufferedSampleCount;
        float secondsPlayed;

        ALCdevice *device;
        ALCcontext *context;

        ALuint buffers[numBuffers];
        ALuint source;

        pthread_t thread;
        pthread_mutex_t mutex;
        pthread_cond_t cond;

        static void *audioThreadTrampoline(void *audioRenderer);

        void *audioThreadHandler();

        AudioFrame *popFrame();
        ALuint waitForProcessedBuffer();
        void consumeFrame(ALuint buffer, AudioFrame *frame);
    public:
        AudioRenderer();
        virtual ~AudioRenderer();

        bool start();
        void stop();

        void pushFrame(const int16_t *samples, int sampleCount, int sampleRate, int channelCount);

        float getSecondsPlayed();
        void resetSecondsPlayed();

        int getBufferSize();
};

AudioRenderer::AudioRenderer() : queuedSampleCount(0), bufferedSampleCount(0), secondsPlayed(0), device(nullptr), context(nullptr) {
}

AudioRenderer::~AudioRenderer() {
    // stop playback
    if (context) {
        stop();
    }
}

bool AudioRenderer::start() {
    // if we already have a context then we are already started
    if (context) {
        return true;
    }

    device = alcOpenDevice(NULL);
    CHECK_ALC_ERRORS("alcOpenDevice");

    context = alcCreateContext(device, NULL);
    CHECK_ALC_ERRORS("alcCreateContext");

    alcMakeContextCurrent(context);
    CHECK_ALC_ERRORS("alcMakeContextCurrent");

    alListenerf(AL_GAIN, 1.0f);
    CHECK_AL_ERRORS("alListenerf");

    alDistanceModel(AL_NONE);
    CHECK_AL_ERRORS("alDistanceModel");

    alGenBuffers(numBuffers, buffers);
    CHECK_AL_ERRORS("alGenBuffers");

    alGenSources(1, &source);
    CHECK_AL_ERRORS("alGenSources");

    pthread_mutex_init(&mutex, NULL);
    pthread_cond_init(&cond, NULL);

    pthread_create(&thread, NULL, audioThreadTrampoline, this);

    return true;
}

void AudioRenderer::stop() {
    // TODO: stub
    //printf("AudioRenderer::stop() stub!\n");
}

void *AudioRenderer::audioThreadTrampoline(void *audioRenderer) {
    return ((AudioRenderer *)audioRenderer)->audioThreadHandler();
}

AudioFrame *AudioRenderer::popFrame() {
    pthread_mutex_lock(&mutex);

    // block until there is something in the queue
    while (audioQueue.empty()) {
        int err = pthread_cond_wait(&cond, &mutex);
        if (err) {
            printf("AudioRenderer cond wait error: %s", strerror(err));
            return nullptr;
        }
    }

    // remove first element
    auto frame = audioQueue.front();
    audioQueue.pop();

    // reduce number of samples in queue
    queuedSampleCount -= frame->sampleCount;

    pthread_mutex_unlock(&mutex);
    return frame;
}

void AudioRenderer::consumeFrame(ALuint buffer, AudioFrame *frame) {
    ALenum format = frame->channelCount == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
    ALsizei size = frame->sampleCount * frame->channelCount * (ALsizei)sizeof(int16_t);

    alBufferData(buffer, format, frame->samples.data(), size, frame->sampleRate);
    CHECK_AL_ERRORS_AND_IGNORE("alBufferData");

    alSourceQueueBuffers(source, 1, &buffer);
    CHECK_AL_ERRORS_AND_IGNORE("alSourceQueueBuffers");

    pthread_mutex_lock(&mutex);
    bufferedSampleCount += frame->sampleCount;
    secondsPlayed += frame->sampleCount * 1.0f / frame->sampleRate;
    pthread_mutex_unlock(&mutex);

    delete frame;
}

ALuint AudioRenderer::waitForProcessedBuffer() {
    ALint processedCount;
    ALuint buffer;

    // wait until there is an empty audio buffer available
    for (;;) {
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processedCount);
        CHECK_AL_ERRORS_AND_IGNORE("alGetSourcei");

        if (processedCount > 0) {
            break;
        }

        usleep(100);
    }

    // get a buffer
    alSourceUnqueueBuffers(source, 1, &buffer);
    CHECK_AL_ERRORS("alSourceUnqueueBuffers");

    // update the number of currently buffered samples
    ALint size;
    alGetBufferi(buffer, AL_SIZE, &size);

    ALint channels;
    alGetBufferi(buffer, AL_CHANNELS, &channels);

    ALint bits;
    alGetBufferi(buffer, AL_BITS, &bits);

    int samples = size * 8 / (channels * bits);
    pthread_mutex_lock(&mutex);
    bufferedSampleCount -= samples;
    pthread_mutex_unlock(&mutex);

    return buffer;
}

void *AudioRenderer::audioThreadHandler() {
    // prebuffer audio
    for (int i=0; i<numBuffers; ++i) {
        consumeFrame(buffers[i], popFrame());
    }

    for (;;) {
        AudioFrame *frame;

        alSourcePlay(source);
        CHECK_AL_ERRORS_AND_IGNORE("alSourcePlay");

        // dequeue and consume audio frames
        for (;;) {
            ALuint buffer = waitForProcessedBuffer();
            frame = popFrame();

            ALint sampleRate;
            alGetBufferi(buffer, AL_FREQUENCY, &sampleRate);

            ALint channelCount;
            alGetBufferi(buffer, AL_CHANNELS, &channelCount);

            if (sampleRate != frame->sampleRate || channelCount != frame->channelCount) {
                // format change, break out of loop to reset stream
                break;
            }

            consumeFrame(buffer, frame);

            // restart the source if we are not playing anymore, this occurs
            // when there is a buffer underrun
            ALenum state;
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            if (state != AL_PLAYING) {
                alSourcePlay(source);
                CHECK_AL_ERRORS_AND_IGNORE("alSourcePlay");
            }
        }

        // we only reach this section if the audio format has changed
        alSourcei(source, AL_BUFFER, 0);
        CHECK_AL_ERRORS_AND_IGNORE("alSourcei");

        alSourceStop(source);
        CHECK_AL_ERRORS_AND_IGNORE("alSourceStop");

        consumeFrame(buffers[0], frame);

        for (int i=1; i<numBuffers; ++i) {
            consumeFrame(buffers[i], popFrame());
        }
    }

    return nullptr;
}

void AudioRenderer::pushFrame(const int16_t *samples, int sampleCount, int sampleRate, int channelCount) {
    // create frame
    AudioFrame *frame = new AudioFrame;
    frame->sampleCount = sampleCount;
    frame->sampleRate = sampleRate;
    frame->channelCount = channelCount;

    // fill sample vector
    size_t shortCount = sampleCount * channelCount;
    frame->samples.reserve(shortCount);
    for (int i=0; i<shortCount; ++i) {
        frame->samples.push_back(samples[i]);
    }

    pthread_mutex_lock(&mutex);

    // enqueue frame
    audioQueue.push(frame);

    // increase amount of samples in queue
    queuedSampleCount += frame->sampleCount;

    pthread_cond_signal(&cond);
    pthread_mutex_unlock(&mutex);
}

float AudioRenderer::getSecondsPlayed() {
    float result;

    pthread_mutex_lock(&mutex);
    result = secondsPlayed;
    pthread_mutex_unlock(&mutex);

    return result;
}

void AudioRenderer::resetSecondsPlayed() {
    pthread_mutex_lock(&mutex);
    secondsPlayed = 0; // FIXME: subtracting seconds still in buffer might give more accuracy. Unsure if this precision is really needed though.
    pthread_mutex_unlock(&mutex);
}

int AudioRenderer::getBufferSize() {
    int result;

    pthread_mutex_lock(&mutex);
    result = bufferedSampleCount + queuedSampleCount;
    pthread_mutex_unlock(&mutex);

    return result;
}

static AudioRenderer audioRenderer;
std::vector<int16_t> prebuffer;

extern "C" {
    void audio_init() {
        printf("initializing audio\n");

        audioRenderer.start();
    }

    void audio_deinit() {
        printf("deinitializing audio\n");

        audioRenderer.stop();
    }

    void audio_feed_sample(double sample) {
        prebuffer.push_back((int16_t)(sample * 32767.0));
        if (prebuffer.size() == 1024) {
            audioRenderer.pushFrame(prebuffer.data(), 1024, 44100, 1);
            prebuffer.clear();
        }
    }

    int audio_get_buffer_size() {
        return audioRenderer.getBufferSize();
    }

    void audio_sleep(double delay) {
        usleep(delay * 1000000);
    }

    float unpack_float(unsigned char a, unsigned char b, unsigned char c, unsigned char d) {
        unsigned char buf[4] = {a, b, c, d};
        float result = *((float *)buf);
        return result;
    }
}
