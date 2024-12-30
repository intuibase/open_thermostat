#pragma once

#include <array>

template <typename T, size_t CAPACITY> class CircularBuffer {
public:
	void push(T const &element) {
		push(std::move(element));
	}

	void push(T &&element) {
		lastPosition_ = storePosition_;
		buffer_[storePosition_] = std::move(element);
		if (size_ < CAPACITY) {
			size_++;
		}

		storePosition_++;
		if (storePosition_ == CAPACITY) {
			storePosition_ = 0;
		}
	}

	T const &operator[](size_t idx) const {
		return buffer_[idx];
	}

	T const &get(size_t idx) const {
		return buffer_[idx];
	}

	size_t size() const {
		return size_;
	}

	size_t getLastIndex() const {
		return lastPosition_;
	}

	bool inline empty() const {
		return size_ == 0;
	}

private:
	size_t lastPosition_ = 0;
	size_t storePosition_ = 0;
	size_t size_ = 0;
	std::array<T, CAPACITY> buffer_;
};
