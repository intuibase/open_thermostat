#pragma once

#include <sstream>
#include <string_view>

namespace ib {

class viewable_stringbuf : public std::basic_stringbuf<char, std::char_traits<char>> {
public:
	std::string_view view() const {
		if (this->pptr()) {
			// The current egptr() may not be the actual string end.
			if (this->pptr() > this->egptr())
				return {this->pbase(), static_cast<size_t>(this->pptr() - this->pbase())}; // TODO c++20 change to iterator constructor
			else
				return {this->pbase(), static_cast<size_t>(this->egptr() - this->pbase())};
		} else {
			return {_M_string.data(), _M_string.length()};
		}
	}
};
} // namespace ib