#include "stdafx.h"
#include "audio.h"

AudioDevice::AudioDevice(){
	// Request opening default device for current system.
	alcOpenDevice(nullptr);
}

audioBuffer::audioBuffer() : Ready(false) {
	// Generate buffer object
	alGenBuffers(1, &ID);
}

audioBuffer::~audioBuffer(){
	alDeleteBuffers(1, &ID);
}

audioBuffer::audioBuffer(audioBuffer && other) noexcept : ID(std::move(other.ID)), Format(std::move(other.Format)), SampleRate(std::move(other.SampleRate)), Ready(std::move(other.Ready)) {
	// make sure to set other.ID = 0
	other.ID = 0;
}

audioBuffer & audioBuffer::operator=(audioBuffer && other) noexcept{
	ID = std::move(other.ID);
	Format = std::move(other.Format);
	SampleRate = std::move(other.SampleRate);
	Ready = std::move(other.Ready);
	other.ID = 0;
	return *this;
}

void audioBuffer::LoadOGG(const char * filename){

	// Going to read in 32KB chunks for buffering
	constexpr size_t bufferSize = 32768;

	// Array buffer
	std::array<char, bufferSize> buffer;

	// Setup standard objects required to read a file.
	int endian = 0;
	int bitStream;
	long bytes;
	FILE *f = std::fopen(filename, "rb");

	// Setup elements required by OpenAL/libvorbis.
	vorbis_info *info;
	OggVorbis_File oggFile;

	// Open file using SDK
	ov_open(f, &oggFile, nullptr, 0);

	// Get file info.
	info = ov_info(&oggFile, -1);

	// Set audio format.
	if (info->channels == 1) {
		Format = AL_FORMAT_MONO16;
	}
	else {
		Format = AL_FORMAT_STEREO16;
	}

	// Set sampling rate.
	SampleRate = info->rate;

	// Now decode data.
	do {

		// Read a chunk of data of size "bufferSize"
		bytes = ov_read(&oggFile, &buffer[0], bufferSize, endian, 2, 1, &bitStream);

		// Add data of "bufferSize" to data container for this buffer object
		for (auto chr : buffer) {
			Data.push_back(chr);
		}

	} while (bytes > 0);

	// Clear opened audio file.
	ov_clear(&oggFile);

}

void audioBuffer::BuildBuffer(){
	alBufferData(ID, Format, &Data[0], static_cast<ALsizei>(Data.size()), SampleRate);
	Ready = true;
}

audioSource::audioSource(const glm::vec3 & pos) : Position(pos) {
	// Generate this source.
	alGenSources(1, &ID);
	// Set position.
	alSource3f(ID, AL_POSITION, Position.x, Position.y, Position.z);
}

audioSource::~audioSource(){
	alDeleteSources(1, &ID);
}

audioSource::audioSource(audioSource && other) noexcept : State(std::move(other.State)), ID(std::move(other.ID)), Position(std::move(other.Position)) {
	other.State = 0;
	other.ID = 0;
}

audioSource & audioSource::operator=(audioSource && other) noexcept{
	State = std::move(other.State);
	ID = std::move(other.ID);
	Position = std::move(other.Position);
	other.State = 0;
	other.ID = 0;
	return *this;
}

void audioSource::SetPosition(const glm::vec3 & pos){
	// Update position of this object
	Position = pos;
	// Update position in AL API
	alSource3f(ID, AL_POSITION, Position.x, Position.y, Position.z);
}

void audioSource::BindBuffer(const ALuint& buffer_id){
	// Attach given sound buffer to this source.
	alSourcei(ID, AL_BUFFER, buffer_id);
}

void audioSource::Play(){
	// Play this object.
	alSourcePlay(ID);
}

audioListener::audioListener(const glm::vec3 & pos) : Position(pos) {
	// Setup this listener.

	// Set position.
	alListener3f(AL_POSITION, Position.x, Position.y, Position.z);
}

void audioListener::SetPosition(const glm::vec3 & pos){
	// Update position of this object.
	Position = pos;
	// Update position in AL API
	alListener3f(AL_POSITION, Position.x, Position.y, Position.z);
}
