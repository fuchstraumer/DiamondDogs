#include "stdafx.h"
#include "TerrainSampler.h"

vulpes::terrain::Modifier::Modifier(ModType modifier_type, Sampler * left, Sampler * right) : Sampler(), ModifierType(modifier_type), leftOperand(left), rightOperand(right) {}

float vulpes::terrain::Modifier::Sample(const size_t & x, const size_t & y) {
	float left_sample = leftOperand->Sample(x, y);
	float right_sample = rightOperand->Sample(x, y);
	float result;
	switch (ModifierType) {
	case ModType::ADD:
		result = left_sample + right_sample;
		break;
	case ModType::SUBTRACT:
		result = left_sample - right_sample;
		break;
	case ModType::MULTIPLY:
		result = left_sample * right_sample;
		break;
	case ModType::DIVIDE:
		result = left_sample / right_sample;
		break;
	case ModType::POW:
		result = std::pow(left_sample, right_sample);
	default:
		std::cerr << "Invalid modifier type specified, sampler will return 0.0" << std::endl;
		break;
	}
	return result;
}

vulpes::terrain::HeightmapSampler::HeightmapSampler(const char * filename, addressMode addr_mode) : Sampler(), AddressMode(addr_mode) {
	heightmap = gli::texture2d(gli::load(filename));
}

float vulpes::terrain::HeightmapSampler::Sample(const size_t & x, const size_t & y) {
	size_t sample_x, sample_y;
	glm::vec4 result;
	switch (AddressMode) {
		case addressMode::CLAMP: {
			gli::sampler2d<glm::vec4> sample(heightmap, gli::wrap::WRAP_CLAMP_TO_EDGE, gli::filter::FILTER_LINEAR, gli::filter::FILTER_LINEAR);
			result = sample.texel_fetch(gli::extent2d(x, y), 0);
			break;
		}
		case addressMode::MIRROR: {
			gli::sampler2d<glm::vec4> sample(heightmap, gli::wrap::WRAP_MIRROR_REPEAT, gli::filter::FILTER_LINEAR, gli::filter::FILTER_LINEAR);
			result = sample.texel_fetch(gli::extent2d(x, y), 0);
			break;
		}
		case addressMode::REPEAT: {
			gli::sampler2d<glm::vec4> sample(heightmap, gli::wrap::WRAP_REPEAT, gli::filter::FILTER_LINEAR, gli::filter::FILTER_LINEAR);
			result = sample.texel_fetch(gli::extent2d(x, y), 0);
			break;
		}
	}
	// return average of sampled pixel: heightmap SHOULD be grayscale, though.
	return (result.x + result.y + result.z + result.w) / 4.0f;
}
