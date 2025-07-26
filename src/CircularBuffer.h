#pragma once

#include <array>

template <typename T, size_t CAPACITY> class CircularBuffer {
public:
	void push(T const &element) {
		buffer_[storePosition_] = element;
		advance();
	}

	void push(T &&element) {
		buffer_[storePosition_] = std::move(element);
		advance();
	}

	T const &operator[](size_t idx) const {
		if (idx >= size_) {
			throw std::out_of_range("CircularBuffer::get");
		}
		return buffer_[idx];
	}

	T const &get(size_t idx) const {
		if (idx >= size_) {
			throw std::out_of_range("CircularBuffer::get");
		}
		return buffer_[idx];
	}

	T const &newest() const {
		if (empty()) {
			throw std::out_of_range("CircularBuffer is empty");
		}
		return buffer_[lastPosition_];
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
	void advance() {
		if (size_ < CAPACITY) {
			size_++;
		}
		lastPosition_ = storePosition_;

		storePosition_++;
		if (storePosition_ == CAPACITY) {
			storePosition_ = 0;
		}
	}

private:
	size_t lastPosition_ = 0;
	size_t storePosition_ = 0;
	size_t size_ = 0;
	std::array<T, CAPACITY> buffer_;
};
