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
