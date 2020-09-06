#pragma once
#include "pool_allocator_base.h"

namespace utility::memory {
	template <class Ty, size_t capacity>
	class StaticPoolAllocator : PoolAllocatorBase<Ty> {
	public:
		using MyBase = PoolAllocatorBase<Ty>;

		using value_type = typename MyBase::value_type;

		using is_always_equal = std::false_type;								//Имеет состояние
		using propagate_on_container_copy_assignment = std::false_type;			//Не копируется при copy assigment
		using propagate_on_container_move_assignment = std::false_type;			//Не может быть перемещён при move assignment
		using propagate_on_container_swap = std::false_type;					//Не поддерживает swap

		template <class OtherTy>
		struct rebind {
			using other = StaticPoolAllocator<OtherTy, capacity>;
		};
	private:
		struct MemoryManagement {
			alignas(Ty) byte storage[capacity * BLOCK_SIZE] = {};
			FreeBlock* ftop{ nullptr };
			size_t offset { 0 };
		};

		struct Stats {
			size_t used_blocks { 0 };
		};
	public:
		StaticPoolAllocator() = default;
		StaticPoolAllocator(const StaticPoolAllocator&) = delete;
		StaticPoolAllocator& operator=(const StaticPoolAllocator&) = delete;
	public:
		Ty* allocate(size_t count) noexcept {
			MyBase::verify_object_count(count);
			Ty* ptr{ reinterpret_cast<Ty*>(allocate_block()) };
			++m_stats.used_blocks;											//Состояние может быть изменено только после удачной аллокации
			return ptr;
		}
		void deallocate(Ty* val, size_t count) noexcept {
			MyBase::verify_object_count(count);
			verify_affiliation(reinterpret_cast<byte*>(val));
			if (reinterpret_cast<byte*>(val) + BLOCK_SIZE == m_memory_management.storage + m_memory_management.offset) {
				m_memory_management.offset -= BLOCK_SIZE;
			}
			else {
				make_free(val);
			}
			--m_stats.used_blocks;
		}
	private:
		byte* allocate_block() {
			if (FreeBlock*& ftop = m_memory_management.ftop; ftop) {
				byte* free_block{ reinterpret_cast<byte*>(ftop) };
				ftop = ftop->prev;
				return free_block;
			}
			verify_storage();
			byte* new_block{ m_memory_management.storage + m_memory_management.offset };
			m_memory_management.offset += BLOCK_SIZE;
			return new_block;
		}

		void make_free(Ty* ptr) {
			m_memory_management.ftop = new (ptr) FreeBlock(m_memory_management.ftop);
		}

		void verify_affiliation(byte* ptr) const {							//Проверяет принадлежность указателя хранилищу аллокатора
			ALLOCATOR_VERIFY(
				ptr >= m_memory_management.storage && ptr <= m_memory_management.storage + capacity * BLOCK_SIZE,
				"Impossible to free an improper block"
			);
		}

		void verify_storage() const {										//Защита от переполнения
			ALLOCATOR_VERIFY(m_memory_management.offset < capacity * BLOCK_SIZE, "Internal buffer overflow");
		}
	private:
		MemoryManagement m_memory_management { MemoryManagement{} };
		Stats m_stats { Stats{} };
	};
}