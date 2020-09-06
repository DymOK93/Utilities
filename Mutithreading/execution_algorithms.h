#pragma once

/*Standart headers*/
#include <thread>
#include <future>
#include <mutex>
#include <vector>
#include <algorithm>
#include <type_traits>

/*C++17 or newer needed*/
namespace utility::execution {
	size_t hardware_thread_count();

	struct Pages {
		size_t size,
			count;
	};
	Pages calculate_page_size(size_t object_count, size_t thread_count);

	template<class ForwardIt, class Function>
	void sequential_for(ForwardIt first, ForwardIt last, Function func) {
		for (; first != last; ++first) {
			func(*first);
		}
	}

	template<class ForwardIt, class Function>
	void parallel_for(ForwardIt first, ForwardIt last, Function func) {
		const size_t thread_count{ hardware_thread_count() };
		if (thread_count < 2) {
			sequential_for(first, last, func);
		}
		else {
			std::vector<std::future<void>> futures;
			size_t residual_size{ static_cast<size_t>(std::distance(first,last)) };
			auto [page_size, page_count] { calculate_page_size(residual_size, thread_count) };
			futures.reserve(page_count);

			while (page_count--) {
				auto bound{ first };
				std::advance(
					bound,
					std::min(page_size, residual_size)
				);
				futures.push_back(
					std::async(
						sequential_for<ForwardIt, Function>,
						first,
						bound,
						func
					)
				);
				first = bound;
				residual_size -= page_size;		//There may be overflow
			}
		}
	}
}