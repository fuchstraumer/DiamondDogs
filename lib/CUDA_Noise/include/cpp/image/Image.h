#pragma once
#ifndef IMAGE_H
#define IMAGE_H
#include "../common/CommonInclude.h"

/*

	Class - ImageWriter

	Writes an image to a file given data. Can write completely uncompressed
	BMP images, mostly uncompressed PNG images, or highly compressed PNG images.

	Uses LodePNG for PNG stuff. BMP stuff is written into this class.

*/

namespace cnoise {

	namespace img {

		class ImageWriter {
		public:

			// Initializes image and allocates space in vector used to hold raw data.
			ImageWriter(int width, int height);

			void FreeMemory();

			// Writes data contained in this image to file with given name.
			void WriteBMP(const char* filename);

			// Writes data contained in this image to PNG. Compression level set by
			// second parameter, but is optional. Defaults to uncompressed.
			void WritePNG(const char* filename, int compression_level = 0);
			
			// Writes png like above, but does so with bit depth of 16.
			void WritePNG_16(const char * filename);

			void WriteRaw32(const char * filename);

			void WriteTER(const char * filename);

			// Sets rawData
			void SetRawData(const std::vector<float>& raw);

			// Gets rawData
			std::vector<float> GetRawData() const;

		private:
			// Holds raw data grabbed from one of the noise modules.
			std::vector<float> rawData;

			// Holds pixel data converted from rawData.
			std::vector<uint8_t> pixelData;

			// Dimensions of this image
			int width, height;

			// Writes BMP header.
			void WriteBMP_Header(std::ofstream& output_stream) const;
		};

	}
}

#endif // !IMAGE_H
