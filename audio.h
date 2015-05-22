#ifndef __AUDIO_H
#define __AUDIO_H

void audio_init();
void audio_deinit();
void audio_feed_sample(double sample);
int audio_get_buffer_size();
void audio_sleep(double delay);
float unpack_float(unsigned char a, unsigned char b, unsigned char c, unsigned char d);

#endif
