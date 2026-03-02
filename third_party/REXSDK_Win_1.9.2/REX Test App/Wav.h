
#ifndef __DLL_WAV_H
#define __DLL_WAV_H

#include <stdio.h>

#if _WIN32
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
#else
#include <stdint.h>
#endif // _WIN32


#ifndef dllFOURCC
#define dllFOURCC( ch0, ch1, ch2, ch3 )				\
		( (uint32_t)(uint8_t)(ch0) | ( (uint32_t)(uint8_t)(ch1) << 8 ) |	\
		( (uint32_t)(uint8_t)(ch2) << 16 ) | ( (uint32_t)(uint8_t)(ch3) << 24 ) )
#endif


#define WAVE_RIFF_ID							dllFOURCC('R','I','F','F')
#define WAVEID									dllFOURCC('W','A','V','E')
#define WAVE_FORMAT_ID							dllFOURCC('f','m','t',' ')
#define WAVE_SOUND_DATA_ID						dllFOURCC('d','a','t','a')

enum {
	WAVE_FORMAT_PCM_ID	=	1
};

typedef int16_t WAVE_MARKER_ID_TYPE;

typedef enum {
	IMAGE_CKID_TAG				=	0,
	IMAGE_CKSIZE_TAG			=	4,
	IMAGE_FORMAT_TAG			=	8,
	IMAGE_CHANNELS				=	10,
	IMAGE_SAMPLES_PER_SEC		=	12,
	IMAGE_AVG_BYTES_PER_SEC		=	16,
	IMAGE_BLOCK_ALIGN			=	20,
	IMAGE_BITS_PER_SAMPLE		=	22,
	IMAGE_SIZE				=	24
} IMAGE;

#define RIFF_IMAGE_SIZE 8

typedef struct {
	uint32_t ckID;
	int32_t ckSize;
} RIFF_FORM_CHUNK;

typedef struct WAVE_PCM_FORMAT_CHUNK {
		uint32_t ckID;
		int32_t ckSize;
		uint16_t formatTag;
		int16_t channels;
		int32_t samplesPerSec;
		uint32_t avgBytesPerSec;
		uint16_t blockAlign;     
		uint16_t bitsPerSample;
} WAVE_PCM_FORMAT_CHUNK;

 
/* These functions are used in our test application */
uint32_t WriteWave(FILE* file,uint32_t wordCount, uint32_t numChannels, uint32_t sampleSize, uint32_t sampleRate,float* buffers[2]);
void WriteTxt(FILE* file,uint32_t wordCount, uint32_t numChannels, uint32_t sampleRate, float* buffers[2]);



#endif
