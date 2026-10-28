#pragma once

#include <string_view>
#include <vector>

namespace arena
{
	inline std::vector<char32_t> decodeUtf8(std::string_view text)
	{
		std::vector<char32_t> out;
		out.reserve(text.size());
		for (size_t i = 0; i < text.size(); )
		{
			const unsigned char c = static_cast<unsigned char>(text[i]);
			char32_t cp = 0xFFFD;
			size_t len = 1;
			if (c < 0x80) { cp = c; }
			else if ((c >> 5) == 0x6 && i + 1 < text.size()) { cp = ((c & 0x1F) << 6) | (text[i + 1] & 0x3F); len = 2; }
			else if ((c >> 4) == 0xE && i + 2 < text.size()) { cp = ((c & 0x0F) << 12) | ((text[i + 1] & 0x3F) << 6) | (text[i + 2] & 0x3F); len = 3; }
			else if ((c >> 3) == 0x1E && i + 3 < text.size()) { cp = ((c & 0x07) << 18) | ((text[i + 1] & 0x3F) << 12) | ((text[i + 2] & 0x3F) << 6) | (text[i + 3] & 0x3F); len = 4; }
			out.push_back(cp);
			i += len;
		}

		return out;
	}
}
