#include "execution_algorithms.h"

namespace utility::execution {
	size_t hardware_thread_count() {
		return static_cast<size_t>(std::thread::hardware_concurrency());	//hardware_concurrency() return type is unsigned int
	}

	Pages calculate_page_size(size_t object_count, size_t thread_count) {
		const size_t page_size{ object_count / thread_count };
		if (!page_size) {
			return { object_count, 1 };
		}
		return {
			page_size,
			object_count / page_size + static_cast<size_t>(static_cast<bool>(object_count % thread_count))
		};
	}
}