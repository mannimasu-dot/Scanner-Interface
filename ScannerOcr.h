#pragma once
#include "ScannerCore.h"

namespace scanner {
struct OcrWord {
	int left = 0, top = 0, width = 0, height = 0;
	std::wstring text;
};
std::vector<OcrWord> ParseOcrTsv(const std::string& data, uint32_t width, uint32_t height);
std::vector<OcrWord> RecognizeText(const Image& image, OcrLanguage language, const std::filesystem::path& work);
}
