/*
A simple sound library for CSE 20211 by Douglas Thain (dthain@nd.edu).
This work is made available under the Creative Commons Attribution license.
https://creativecommons.org/licenses/by/4.0/

For course assignments, you should not change this file.
For complete documentation, see:
http://www.nd.edu/~dthain/courses/cse20211/fall2013/wavfile
*/
//Changes for integration in the UABE AudioClip plugin:
// - Use IAssetsWriter instead of FILE* as an abstraction.
// - Allow different formats based on WAVFILE_SOUND_FORMAT.

#include "wavfile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma pack(push, 1)
struct wavfile_header {
	char      riff_tag[4];
	uint32_t  riff_length;
	char      wave_tag[4];
	char      fmt_tag[4];
	uint32_t  fmt_length;
	uint16_t  audio_format;
	uint16_t  num_channels;
	uint32_t  sample_rate;
	uint32_t  byte_rate;
	uint16_t  block_align;
	uint16_t  bits_per_sample;
	char      data_tag[4];
	uint32_t  data_length;
};
#pragma pack(pop)
enum EnumWaveFormats
{
	WaveFmt_PCM=1,
	WaveFmt_FLOAT=3,
	WaveFmt_IMAADPCM=17,
	WaveFMT_MP1_2=80,
	WaveFMT_MP3=85,
};

IAssetsWriter *wavfile_open( IAssetsWriter *file, WAVFILE_SOUND_FORMAT format, uint32_t sampleRate, uint32_t channelCount )
{
	struct wavfile_header header;
	int bits_per_sample;
	switch (format)
	{
		case WAVFILE_SOUND_FORMAT::PCM_8bit:
			bits_per_sample = 8;
			header.audio_format = WaveFmt_PCM;
			break;
		case WAVFILE_SOUND_FORMAT::PCM_16bit:
			bits_per_sample = 16;
			header.audio_format = WaveFmt_PCM;
			break;
		case WAVFILE_SOUND_FORMAT::PCM_24bit:
			bits_per_sample = 24;
			header.audio_format = WaveFmt_PCM;
			break;
		case WAVFILE_SOUND_FORMAT::PCM_32bit:
			bits_per_sample = 32;
			header.audio_format = WaveFmt_PCM;
			break;
		default:
			return NULL;
	}


	strncpy(header.riff_tag,"RIFF",4);
	strncpy(header.wave_tag,"WAVE",4);
	strncpy(header.fmt_tag,"fmt ",4);
	strncpy(header.data_tag,"data",4);

	header.riff_length = 0;
	header.fmt_length = 16;
	header.num_channels = (uint16_t)channelCount;
	header.sample_rate = sampleRate;
	header.block_align = channelCount*((bits_per_sample+7)/8);
	header.byte_rate = sampleRate*header.block_align;
	header.bits_per_sample = bits_per_sample;
	header.data_length = 0;

	file->Write(0x2C, &header);
	file->Flush();

	return file;
}

void wavfile_write( IAssetsWriter *file, void *data, uint32_t byteLen )
{
	file->Write(byteLen, data);
}

void wavfile_close( IAssetsWriter *file )
{
	QWORD file_length = 0;
	file->Tell(file_length);
	if (file_length < 0x7FFFFFFF)
	{
		uint32_t data_length = file_length - sizeof(struct wavfile_header);
		if (data_length < file_length)
		{
			file->Seek(AssetsSeek_Begin, sizeof(struct wavfile_header) - sizeof(int));
			file->Write(sizeof(data_length), &data_length);

			uint32_t riff_length = file_length - 8;
			file->Seek(AssetsSeek_Begin, 4);
			file->Write(sizeof(riff_length), &riff_length);
		}
	}
}
