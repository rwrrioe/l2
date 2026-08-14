// This implementation is mainly inspired by the fabulous
// Rigtorp's SPSCQ but simplified for own convenience.

#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>


template<typename T, typename Allocator = std::allocator<T>>
class SPSQ {
public:
	explicit SPSQ(const size_t capacity,
				const Allocator &allocator = Allocator())
		: capacity_(capacity), allocator_(allocator)
	{
		if (capacity_ < 1) {
			capacity_ = 1;
		}
		capacity_++;
		if (capacity_ > SIZE_MAX - 2 * padding_) {
			capacity_ = SIZE_MAX - 2 * padding_;
		}

		slots_ = std::allocator_traits <Allocator>::allocate(
			allocator_, capacity_ + 2 * padding_);

		static_assert(alignof(SPSQ<T>) == cacheLineSize_, "");
		static_assert(sizeof(SPSQ<T>) >= 3 * cacheLineSize_, "");

		assert(reinterpret_cast<char*>(&read_idx_) -
			reinterpret_cast<char*>(&write_idx_) >=
			static_cast<std::ptrdiff_t>(cacheLineSize_)
		);
	}

	~SPSQ() {
		while (front()) {
			pop();
		}

		std::allocator_traits<Allocator>::deallocate(allocator_, slots_,
			capacity_ + 2 * padding_);
	}

	template <typename... Args>
	void emplace(Args &&...args) noexcept {
		auto const write_idx = write_idx_.load(std::memory_order_relaxed);
		auto next_write_idx = write_idx + 1;

		if (next_write_idx == capacity_) {
			next_write_idx = 0;
		}

		while (next_write_idx == read_idx_cache_) {
			read_idx_cache_ = read_idx_.load(std::memory_order_acquire);
		}

		new (&slots_[write_idx + padding_]) T(std::forward<Args>(args)...);
		write_idx_.store(next_write_idx, std::memory_order_release);
	}

	template<typename... Args>
	bool try_emplace(Args &&...args) noexcept {
		auto const write_idx = write_idx_.load(std::memory_order_relaxed);
		auto next_write_idx = write_idx + 1;

		if (next_write_idx == capacity_) {
			next_write_idx = 0;
		}


		if (next_write_idx == read_idx_cache_) {
			read_idx_cache_ = read_idx_.load(std::memory_order_acquire);

			if (next_write_idx == read_idx_cache_) {
				return false;
			}
		}

		new (&slots_[write_idx + padding_]) T(std::forward<Args>(args)...);
		write_idx_.store(next_write_idx, std::memory_order_release);
		return true;
	}


	void push(const T& v) noexcept {
		emplace(v);
	}

	bool try_push(const T& v) noexcept(std::is_nothrow_constructible<T>::value) {
		return try_emplace(v);
	}


	T* front() noexcept {
		auto const read_idx = read_idx_.load(std::memory_order_relaxed);

		if (read_idx == write_idx_cache_) {
			write_idx_cache_ = write_idx_.load(std::memory_order_acquire);

			if (read_idx == write_idx_cache_) {
				return nullptr;
			}
		}

		return &slots_[read_idx + padding_];
	}

	void pop() noexcept {
		auto const read_idx = read_idx_.load(std::memory_order_relaxed);
		slots_[read_idx + padding_].~T();

		auto next_read_idx = read_idx + 1;
		if (next_read_idx == capacity_) {
			next_read_idx = 0;
		}

		read_idx_.store(next_read_idx, std::memory_order_release);
	}

	bool is_empty () const noexcept {
		return write_idx_.load(std::memory_order_acquire) ==
			read_idx_.load(std::memory_order_acquire);
	}

private:
	size_t capacity_;
	T* slots_;
	Allocator allocator_;

	static constexpr size_t cacheLineSize_ = 64;
	static constexpr size_t padding_ = (cacheLineSize_ - 1) / sizeof(T) + 1;

	alignas(cacheLineSize_) std::atomic<size_t> write_idx_ = { 0 };
	alignas(cacheLineSize_) size_t read_idx_cache_ = 0;
	alignas(cacheLineSize_) std::atomic<size_t> read_idx_ = { 0 };
	alignas(cacheLineSize_) size_t write_idx_cache_ = 0;
};
