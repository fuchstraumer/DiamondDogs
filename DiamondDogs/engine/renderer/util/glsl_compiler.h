#pragma once
#ifndef VULPES_GLSL_COMPILER_H
#define VULPES_GLSL_COMPILER_H

#include "stdafx.h"

namespace vulpes::util {

	static void find_includes(const std::string& input, std::vector<std::string>& output) {
		static const std::basic_regex<char> re("^[ ]*#[ ]*include[ ]+[\"<](.*)[\">].*");
		std::stringstream input_stream;
		input_stream << input;
		std::smatch matches;
		// Current line
		std::string line;
		while (std::getline(input_stream, line)) {
			if (std::regex_search(line, matches, re)) {
				std::string include_file = matches[1];
				std::string include_string;
				std::ifstream include_stream;
				// Try to open included file.
				try {
					std::string path = "./shaders/include/" + include_file;
					include_stream.open(path);
					std::stringstream tmp;
					tmp << include_stream.rdbuf();
					include_string = tmp.str();
					include_stream.close();
				}
				catch (std::ifstream::failure e) {
					std::cerr << "ERROR::SHADER::INCLUDED_FILE_NOT_SUCCESFULLY_READ: " << include_file << std::endl;
				}
				output.push_back(include_string);
			}
		}
	}
	
	struct compiler {

		// Used to find/retrieve uniforms.
		using uniforms_map = std::unordered_map<std::string, GLuint>;
		// Used to find/retrieve shader objects.
		using shaders_map = std::unordered_map<GLuint, std::string>;

		// psuedo "command line" struct
		struct cl {
			enum class profile {
				CORE,
				COMPATABILITY,
				DEBUG,
			};

			cl(const std::string& filename, const std::string& args) {

			}

			std::string profile;
			int version;
			std::vector<std::string> defines;
			std::vector<std::string> includes;
		};
	};

}

#endif // !VULPES_GLSL_COMPILER_H
