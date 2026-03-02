
#include <stdio.h>
#include <assert.h>

#include "Wav.h"

/* Functions local to this file*/
void PackPCM(WAVE_PCM_FORMAT_CHUNK* chunk,uint8_t binary[]);
void PackRIFF(RIFF_FORM_CHUNK* riff,uint8_t binary[]);

uint32_t WritePCMFormatChunk(FILE* file,uint32_t channels, uint32_t sampleSize, uint32_t rate);
uint32_t WriteSoundDataChunk(FILE* file,uint32_t wordCount, uint32_t numChannels, uint32_t sampleSize,float* buffers[2]);

void Pack32BitUnsignedLittle(uint8_t binary[], uint32_t v);
void Pack16BitUnsignedLittle(uint8_t binary[], uint16_t v);

int16_t ScaleAndClip(float f);


/*
	Pack16BitUnsignedLittle,
*/
void Pack16BitUnsignedLittle(uint8_t binary[], uint16_t v){
	binary[0]=(uint8_t)(v);
	binary[1]=(uint8_t)(v >> 8);
}

/*
	Pack32BitUnsignedLittle,
*/
void Pack32BitUnsignedLittle(uint8_t binary[], uint32_t v){
	binary[0]=(uint8_t)(v);
	binary[1]=(uint8_t)(v >> 8);
	binary[2]=(uint8_t)(v >> 16);
	binary[3]=(uint8_t)(v >> 24);
}


/*
	WriteWave,
	WriteWave takes buffers to sample data, an open FILE stream and some parameters.
	It limits the input data and writes a wav-format file to the file stream.
*/
uint32_t WriteWave(FILE* file,uint32_t wordCount, uint32_t numChannels, uint32_t sampleSize,uint32_t sampleRate,float* buffers[2]) {

	uint32_t totalSize=0;

	uint8_t riffFormatImage[RIFF_IMAGE_SIZE];
	RIFF_FORM_CHUNK riff;
	long riffPos=ftell(file);

	/* RIFF chunk*/
	riff.ckID=WAVE_RIFF_ID;
	riff.ckSize=0;
	PackRIFF(&riff,riffFormatImage);
	fwrite(riffFormatImage,RIFF_IMAGE_SIZE,1,file);
	{
		/* Wave head */
		uint8_t waveID[4]; 
		Pack32BitUnsignedLittle(waveID,WAVEID);
		fwrite(waveID,4,1,file);
		totalSize+=4;
		/* fmt chunk */
		{
			totalSize+=WritePCMFormatChunk(file,numChannels,sampleSize,sampleRate);
		}
		/* data chunk */
		{
			totalSize+=WriteSoundDataChunk(file,wordCount,numChannels,sampleSize,buffers);
		}
	}

	fseek(file,riffPos,SEEK_SET);
	riff.ckSize=totalSize;
	PackRIFF(&riff,riffFormatImage);
	fwrite(riffFormatImage,RIFF_IMAGE_SIZE,1,file);

	return(0);
}

/*
	ScaleAndClip,
	Scales and clips float sample data. 	
*/
int16_t ScaleAndClip(float f){

	if (f>=1.0) {
		return(32767);
	} else if (f<=-1.0) {
		return(-32768);
	} else {
		return ((int16_t) ( 32767.0 * f));
	}

}

/*
	WriteSoundDataChunk,
	Writes sound data chunk to file. 	
*/
uint32_t WriteSoundDataChunk(FILE* file,uint32_t wordCount, uint32_t numChannels, uint32_t sampleSize,float* buffers[2]){
	
	uint8_t dataImage[RIFF_IMAGE_SIZE];
	RIFF_FORM_CHUNK data;
	uint32_t pos=0;
	long endPos=0;
	long dataSize=0;

	long dataPos=ftell(file);
	assert(sampleSize==16);

	data.ckID=WAVE_SOUND_DATA_ID;
	data.ckSize=0;
	PackRIFF(&data,dataImage);
	fwrite(dataImage,RIFF_IMAGE_SIZE,1,file);
	

	if (numChannels==1) {
		float* left=buffers[0];
		for (pos=0;pos<wordCount;pos++) {
			uint8_t sample[2];
			int16_t temp=ScaleAndClip(*left++);
			Pack16BitUnsignedLittle(sample,temp);
			fwrite(sample,sizeof(int16_t),1,file);
		}
	} else if (numChannels==2) {
		float* left=buffers[0];
		float* right=buffers[1];

		for (pos=0;pos<wordCount;pos++) {
			uint8_t sample[2];
			int16_t temp=(int16_t) ScaleAndClip(*left++);
			Pack16BitUnsignedLittle(sample,temp);
			fwrite(sample,sizeof(int16_t),1,file);
			temp=(int16_t) ScaleAndClip(*right++);
			Pack16BitUnsignedLittle(sample,temp);
			fwrite(sample,sizeof(int16_t),1,file);
		}
	}

	endPos=ftell(file);

	fseek(file,dataPos,SEEK_SET);
	/* ckSize only includes chunk size, not chunk header size */
	dataSize=endPos-dataPos-8;  
	data.ckSize=(int32_t)dataSize;
	assert(dataSize == (long)data.ckSize);
	PackRIFF(&data,dataImage);
	fwrite(dataImage,RIFF_IMAGE_SIZE,1,file);
	fseek(file,endPos,SEEK_SET);

	return (uint32_t)(endPos-dataPos);
}

/*
	WritePCMFormatChunk,
	Writes PCM format chunk to file. It has a fixed size. 	
*/
uint32_t WritePCMFormatChunk(FILE* file,uint32_t channels, uint32_t sampleSize, uint32_t rate) {

	WAVE_PCM_FORMAT_CHUNK PCMFormat;
	uint8_t pcmFormatImage[IMAGE_SIZE];

	assert(channels > 0);
	assert(sampleSize > 0);
	assert(rate > 0);

	PCMFormat.ckID=WAVE_FORMAT_ID;
	PCMFormat.ckSize=16;

	PCMFormat.formatTag = WAVE_FORMAT_PCM_ID;
	PCMFormat.channels = (uint16_t)channels;
	PCMFormat.samplesPerSec = rate;
	PCMFormat.avgBytesPerSec = channels * rate * sampleSize / 8;
	PCMFormat.blockAlign = (uint16_t)(channels * sampleSize / 8);
	PCMFormat.bitsPerSample = (uint16_t)sampleSize;

	PackPCM(&PCMFormat,pcmFormatImage);
	fwrite(pcmFormatImage,IMAGE_SIZE,1,file);

	return(IMAGE_SIZE);

}

/*
	PackPCM,
*/
void PackPCM(WAVE_PCM_FORMAT_CHUNK* chunk,uint8_t binary[]) {
	Pack32BitUnsignedLittle(&binary[IMAGE_CKID_TAG],chunk->ckID);
	Pack32BitUnsignedLittle(&binary[IMAGE_CKSIZE_TAG],chunk->ckSize);
	Pack16BitUnsignedLittle(&binary[IMAGE_FORMAT_TAG],chunk->formatTag);
	Pack16BitUnsignedLittle(&binary[IMAGE_CHANNELS],chunk->channels);
	Pack32BitUnsignedLittle(&binary[IMAGE_SAMPLES_PER_SEC],chunk->samplesPerSec);
	Pack32BitUnsignedLittle(&binary[IMAGE_AVG_BYTES_PER_SEC],chunk->avgBytesPerSec);
	Pack16BitUnsignedLittle(&binary[IMAGE_BLOCK_ALIGN],chunk->blockAlign);
	Pack16BitUnsignedLittle(&binary[IMAGE_BITS_PER_SAMPLE],chunk->bitsPerSample);
}

/*
	PackRIFF,
*/
void PackRIFF(RIFF_FORM_CHUNK* riff,uint8_t binary[]){
	Pack32BitUnsignedLittle(&binary[IMAGE_CKID_TAG],riff->ckID);
	Pack32BitUnsignedLittle(&binary[IMAGE_CKSIZE_TAG],riff->ckSize);
}


/*
	WriteTxt,
	WriteTxt takes buffers to sample data, an open FILE stream and some parameters.
	It writes the float sample values to the file stream.
	
	Format is:
	
	number of channels
	sample rate
	number of frames
	sample 1
	sample 2
	sample 3
	.
	.
	.
	
	The samples are written interleaved and
		if mono (i.e number of channels is 1) the number of samples equals number of frames.
		If stereo (i.e number of channels is 2) the number of samples equals 2*numberOfFrames.
	
*/
void WriteTxt(FILE* file, uint32_t numberOfFrames, uint32_t numChannels, uint32_t sampleRate, float* buffers[2]) {

	fprintf(file, "%d\n", numChannels);
	fprintf(file, "%d\n", sampleRate);
	fprintf(file, "%d\n", numberOfFrames);
	
	for(uint32_t i=0; i<numberOfFrames; i++) {
		if(numChannels == 1) {
			fprintf(file, "%f\n", buffers[0][i]);
		}
		else {
			assert(numChannels==2);
			fprintf(file, "%f\n", buffers[0][i]);
			fprintf(file, "%f\n", buffers[1][i]);
		}
	}
}
