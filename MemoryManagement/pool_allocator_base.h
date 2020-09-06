#pragma once
#include "memory_management.h"

#include <cassert>
#include <algorithm>

namespace utility::memory {
	template <class Ty>
	class PoolAllocatorBase {
	public:
		using value_type = Ty;
	public:
		constexpr PoolAllocatorBase() = default;
	protected:
		static constexpr size_t BLOCK_SIZE{ std::max(sizeof(Ty), sizeof(FreeBlock)) },	//Размеры объектов хранящегося в аллокаторе типа
								HEADER_SIZE{ sizeof(Page) };					//Размер заголовка страницы памяти
		static constexpr std::align_val_t PAGE_ALIGMENT{ 8 };					//Выравнивание памяти, выделяемой под страницу
	protected:
		static void verify_object_count(size_t count) {							//Аллокатор не поддерживает множественную аллокацию и деаллокацию
			ALLOCATOR_VERIFY(count == 1, "Multiple object allocation and deallocation isn't supported");
		}
	};
}