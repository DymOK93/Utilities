#pragma once
#include <memory>

namespace utility::memory {
	namespace switchable_allocator {
		template <class AllocWithState, class AllocWithoutState>
#if (defined(_MSC_VER) && defined(_DEBUG))
		using AlTy = AllocWithoutState;					//MS STL doesn't support allocators with state in DEBUG mode
#else
		using AlTy = AllocWithState;
#endif
	}
}
