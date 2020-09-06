#pragma once

#include <memory>
#include <type_traits>

namespace utility::memory {
#ifdef _DEBUG
	#define ALLOCATOR_VERIFY(cond, what) assert(cond && what)
	#define CONSTEXPR_ALLOCATOR_VERIFY(cond, what) static_assert(cond, what)
#else
	#define ALLOCATOR_VERIFY(cond, what) 
	#define CONSTEXPR_ALLOCATOR_VERIFY(cond, what) 
#endif
	using byte = unsigned char;

	struct Page {															//Структура заголовка страницы памяти
		constexpr Page(size_t bytes_count, Page* link = nullptr) 
			: size{ bytes_count }, prev{ link } 
		{
		}
		size_t offset{ 0 };													//offset и size - в байтах, размер заголовка не учитывается!
		size_t size;
		Page* prev;
	};

	struct FreeBlock {														//Структура заголовка освобожденного блока
		constexpr FreeBlock(FreeBlock* link) 
			: prev{ link } 
		{
		}
		FreeBlock* prev;
	};

	namespace traits {
		template <class Alloc, typename size_type, class = void>
		struct supports_multiple_allocate
			: std::false_type {};

		template <class Alloc, typename size_type>
		struct supports_multiple_allocate<
			Alloc,
			size_type,
			std::void_t<decltype(std::declval<Alloc>().allocate(std::declval<size_type>()))>
		> : std::true_type {};

		template <class Alloc, typename size_type>
		inline constexpr bool supports_multiple_allocate_v = supports_multiple_allocate<Alloc, size_type>::value;

		template <class Alloc, typename size_type, class = void>
		struct supports_multiple_deallocate
			: std::false_type {};

		template <class Alloc, typename size_type>
		struct supports_multiple_deallocate<
			Alloc,
			size_type,
			std::void_t<decltype(
				std::declval<Alloc>().deallocate(
					std::declval<std::add_pointer_t<typename Alloc::value_type>>(), std::declval<size_type>()
				)
				)>> : std::true_type{};

		template <class Alloc, typename size_type>
		inline constexpr bool supports_multiple_deallocate_v = supports_multiple_allocate<Alloc, size_type>::value;
	}
}