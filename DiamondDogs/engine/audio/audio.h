#pragma once
#ifndef DDOGS_AUDIO_H
#define DDOGS_AUDIO_H

/*
	
	AUDIO_MANAGER_H

	Manages loading audio from files and setting up the appropriate
	buffers for an OpenAL player to use, along with manager the 
	various OpenAL players and updating their parameters.

	Designed to interface with the Context class, taking key 
	data from that: things like listener/viewer location, world
	state, and so on.


*/

#include "stdafx.h"
#include "al.h"
#include "alc.h"
#include "vorbis\vorbisfile.h"



class AudioDevice {
public:

	AudioDevice();

	~AudioDevice();

private:

};

// Buffer object for holding audio data.
struct audioBuffer {

	audioBuffer();
	~audioBuffer();

	// Don't allow copying
	audioBuffer(const audioBuffer& other) = delete;
	audioBuffer& operator=(const audioBuffer& other) = delete;

	// Move is alright
	audioBuffer(audioBuffer&& other) noexcept;
	audioBuffer& operator=(audioBuffer&& other) noexcept;
	
	// ID/Handle of this buffer
	ALuint ID;

	// Format of this audio
	ALenum Format;

	// Sample rate in Hz (usually 44100-48000)
	ALsizei SampleRate;

	// Whether or not buffers for this object have been built
	bool Ready;

	// Audio data.
	std::vector<char> Data;

	// Load an OGG file and use it to populate Data.
	void LoadOGG(const char* filename);

	// Prepare buffers to be used for playback
	void BuildBuffer();

};

// Object for holding data about a source used to play audio data.
struct audioSource {

	// Default constructor.
	audioSource(const glm::vec3& pos = glm::vec3(0.0f));

	// Default destructor.
	~audioSource();

	// No copying
	audioSource(const audioSource& other) = delete;
	audioSource& operator=(const audioSource& other) = delete;

	audioSource(audioSource&& other) noexcept;
	audioSource& operator=(audioSource&& other) noexcept;

	// Set position of this audio source.
	void SetPosition(const glm::vec3& pos);

	// Bind an audio buffer to this source.
	void BindBuffer(const ALuint& buffer_id);

	// Play audio from this source.
	void Play();

	// State of this audio source.
	ALint State;

	// ID/Handle of this audio source
	ALuint ID;

	// Position of this audio source in the world.
	glm::vec3 Position;

};

// Object for holding data about a listener that hears/listen to the players
struct audioListener {

	// Default constructor.
	audioListener(const glm::vec3& pos = glm::vec3(0.0f));

	// Set this listeners position.
	void SetPosition(const glm::vec3& pos);

	// Position of this listener in the world.
	glm::vec3 Position;
};

// Storage objects for audio items

// Buffer data
std::vector<audioBuffer> audioBuffers;

// Source data
std::vector<audioSource> audioSources;


#endif // !AUDIO_MANAGER_H
