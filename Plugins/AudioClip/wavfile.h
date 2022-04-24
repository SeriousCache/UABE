/*
A simple sound library for CSE 20211 by Douglas Thain.
This work is made available under the Creative Commons Attribution license.
https://creativecommons.org/licenses/by/4.0/

For course assignments, you should not change this file.
For complete documentation, see:
http://www.nd.edu/~dthain/courses/cse20211/fall2013/wavfile
*/
//Changes for integration in the UABE AudioClip plugin:
// - Use IAssetsWriter instead of FILE* as an abstraction.
// - Allow different formats based on FMOD_SOUND_FORMAT.

#ifndef WAVFILE_H
#define WAVFILE_H

#include <stdio.h>
#include <AssetsFileReader.h>
#include <cstdint>

enum class WAVFILE_SOUND_FORMAT
{
	PCM_8bit,
	PCM_16bit,
	PCM_24bit,
	PCM_32bit
};

IAssetsWriter *wavfile_open( IAssetsWriter *file, WAVFILE_SOUND_FORMAT format, uint32_t sampleRate, uint32_t channelCount );
void wavfile_write( IAssetsWriter *file, void *data, uint32_t byteLen );
void wavfile_close( IAssetsWriter *file );

#define WAVFILE_SAMPLES_PER_SECOND 44100

#endif