/***********************************
v1.6
Partially STL-compatible
C++17 required
------------------------------------
Dmitry Bolshakov, September 2020
***********************************/
#pragma once
#include "pool_allocator_base.h"

#include <tuple>
#include <memory>
#include <cassert>
#include <algorithm>
#include <type_traits>

namespace memory {
	template <class Ty>
	class PoolAllocator : PoolAllocatorBase<Ty> {
	public:
		using MyBase = PoolAllocatorBase<Ty>;
		using value_type = typename MyBase::value_type;

		using is_always_equal = std::false_type;								//Имеет состояние
		using propagate_on_container_copy_assignment = std::false_type;			//Не копируется при copy assigment
		using propagate_on_container_move_assignment = std::true_type;			//Должен быть перемещён при move assignment
		using propagate_on_container_swap = std::true_type;						//Поддерживает swap

		template <class OtherTy>
		struct rebind {
			using other = PoolAllocator<OtherTy>;
		};
	private:
		struct MemoryManagement {
			Page* base{ nullptr },										//Первая, верхняя и резервная станицы
				* top{ nullptr },
				* reserved_page{ nullptr };
			FreeBlock* ftop{ nullptr };										//Верхний блок в цепочке освобожденных блоков
		};
		struct Stats {
			size_t allocated_blocks{ 0 },
				used_blocks{ 0 };
			bool force_page_write{ false };									//Временное игнорирование освобожденных блоков и запись только в страницу
		};
	private:
		static constexpr double RESERVE_MULTIPLIER{ 1 };						//Коэффициент резервирования								
		static constexpr size_t MIN_ALLOCATED_BLOCKS{ 1 };						//Минимальное число блоков на странице
	public:
		bool operator==(const PoolAllocator& other) const noexcept {
			const auto& mm{ m_memory_management },
				o_mm{ other.m_memory_management };
			return std::tie(
				mm.base, mm.top, mm.reserved_page, mm.ftop,
				m_stats.allocated_blocks, m_stats.used_blocks, m_stats.force_page_write
			) == std::tie(
				o_mm.base, o_mm.top, o_mm.reserved_page, o_mm.ftop,
				other.m_stats.allocated_blocks, other.m_stats.used_blocks, other.m_stats.force_page_write
			);
		}
		bool operator!=(const PoolAllocator& other) const noexcept {
			return !(*this == other);
		}
	public:
		PoolAllocator() = default;
		PoolAllocator(const PoolAllocator&) = delete;
		PoolAllocator& operator=(const PoolAllocator&) = delete;
		PoolAllocator(PoolAllocator&& other) noexcept
			: m_memory_management{ std::exchange(other.m_memory_management, MemoryManagement{}) },
			m_stats{ std::exchange(other.m_stats, Stats{}) }
		{
		}
		PoolAllocator& operator=(PoolAllocator&& other) noexcept {
			if (this != std::addressof(other)) {
				m_memory_management = std::exchange(other.m_memory_management, MemoryManagement{});
				m_stats = std::exchange(other.m_stats, Stats{});
			}
			return *this;
		}
		~PoolAllocator() noexcept { reset(); }

		void swap(PoolAllocator& other) noexcept {
			std::swap(m_memory_management, other.m_memory_management);
			std::swap(m_stats, other.m_stats);
		}
	public:
		Ty* allocate(size_t count) {											//Возвращает указатель на память для одного элемента
			MyBase::verify_object_count(count);
			++m_stats.used_blocks;
			return reinterpret_cast<Ty*>(allocate_block());
		}
		void deallocate(Ty* val, size_t count) noexcept {						//Освобождает память
			MyBase::verify_object_count(count);
            if (Page*& top = m_memory_management.top;                           //Если узел - последний элемент верхней страницы
                reinterpret_cast<byte*>(val) + MyBase::BLOCK_SIZE == reinterpret_cast<byte*>(top) + MyBase::HEADER_SIZE + top->offset) {
				top->offset -= MyBase::BLOCK_SIZE;								//Даже если узел - первый элемент на странице, то оффсет был сдвинут на block_size при его создании
				if (!top->offset) {												//Освобождение пустой верхней страницы
					Page* empty_page{ top };
					m_stats.allocated_blocks -= empty_page->size / MyBase::BLOCK_SIZE;
					top = top->prev;
					deallocate_page(empty_page);
					if (Page*& reserved_page = m_memory_management.reserved_page;
						reserved_page) {
						m_stats.allocated_blocks -= reserved_page->size / MyBase::BLOCK_SIZE;	//Не забываем скорректировать число выделенных блоков
						deallocate_page(reserved_page);							//После удаления пустой верхней страницы целесообразно освободить резервную
						reserved_page = nullptr;
					}
					if (m_memory_management.base == empty_page) {
						top = nullptr;
						m_memory_management.base = nullptr;				
					}
				}
			}
			else {
				make_free(val);
			}
			--m_stats.used_blocks;
		}

		void reserve(size_t	val_count) {										//Принудительно выделяет память в указанном размере
			if (val_count > 0) {
                Page *&top {m_memory_management.top},
                    *&reserved_page {m_memory_management.reserved_page};
                size_t new_blocks_count{ val_count - ((top->size - top->offset) / MyBase::BLOCK_SIZE) };	//Вычисляем кол-во блоков, которые нужно создать
				if (new_blocks_count > 0) {
                    if (!reserved_page || reserved_page->size / MyBase::BLOCK_SIZE < new_blocks_count) { //Резервной страницы нет или ее размер меньше требуемого?
						if (reserved_page) {														//Если резервная страница все-таки есть, но неподходящего размера...
                             m_stats.allocated_blocks -= reserved_page->size;						//...Корректируем счетчик выделенной памяти и удаляем резервную страницу
							deallocate_page(reserved_page);
						}
                        reserved_page = allocate_page(new_blocks_count * MyBase::BLOCK_SIZE);		//Создаем новую резервную страницу
                        m_stats.allocated_blocks += new_blocks_count;								//Увеличиваем счетчик выделенной памяти
					}
					if (top->offset == top->size) {													//Если top заполнена, резервная страница становится верхней...
						reserved_page->prev = top;
						top = reserved_page;
						reserved_page = nullptr;													//...И перестает быть резервной
					}
				}
                m_stats.force_page_write = true;													//Форсируем запись в страницу вместо освобожденных ранее блоков
			}
		}

		void reset() noexcept {													//Принудительно свобождает память, кроме первой страницы
			Page* mpage;
			Page*& top{ m_memory_management.top };

			while (top)	{														//Удаляем все страницы
				mpage = top;
				top = top->prev;
				deallocate_page(mpage);
			}
			deallocate_page(m_memory_management.reserved_page);					//Удаляем резервную страницу. Даже если reserved_page == nullptr, вызов delete безопасен
			m_memory_management = {};											//Не забываем обнулить указатели - память освобождена!
			m_stats = {};
		}

	private:
		byte* allocate_block() {												//Возвращает указатель на ближайший свободный блок
			byte* block;
			if (!m_stats.force_page_write && m_memory_management.ftop) {		//Освобожденные блоки - в приоритете
				block = reinterpret_cast<byte*>(m_memory_management.ftop);
				m_memory_management.ftop = m_memory_management.ftop->prev;
			}
			else {
				Page*& top{ m_memory_management.top };
				if (Page*& reserved_page = m_memory_management.reserved_page;
					!top || top->offset == top->size)
				{
					Page* new_page;
					if (reserved_page) {											//Если верхняя страница заполнена, проверяем, существует ли резервная
						new_page = reserved_page;								//Существует? Отлично!
						reserved_page = nullptr;
					}
					else {															//Агрессивность выделения памяти определяется коэффициентом reserve_multiplier
						size_t new_blocks_count = MIN_ALLOCATED_BLOCKS + m_stats.allocated_blocks * RESERVE_MULTIPLIER;
						new_page = allocate_page(new_blocks_count * MyBase::BLOCK_SIZE);
						m_stats.allocated_blocks += new_blocks_count;			//Если резервной страницы нет, придется выделять память
						m_stats.force_page_write = false;
					}
					new_page->prev = top;
					top = new_page;												//Обновляем указатель на верхнюю страницу
					if (!m_memory_management.base) {
						m_memory_management.base = new_page;
					}
				}
                block = (reinterpret_cast<byte*>(top)) + MyBase::HEADER_SIZE + top->offset;
				top->offset += MyBase::BLOCK_SIZE;										//Смещаем метку на размер блока
			}
			return block;
		}
		Page* allocate_page(size_t page_size) {							//Создает страницу 
            byte* new_page{ static_cast<byte*>(operator new(MyBase::HEADER_SIZE + page_size, MyBase::PAGE_ALIGMENT)) };
			return new (new_page) Page(page_size, m_memory_management.top);
		}
		void deallocate_page(Page* page) noexcept {
            operator delete(page, MyBase::PAGE_ALIGMENT);								//Удаляет страницу
		}
		void make_free(Ty* ptr) noexcept {										//Вносит блок в список освобожденных
			m_memory_management.ftop = new (ptr) FreeBlock(m_memory_management.ftop);
		}
	private:
		MemoryManagement m_memory_management;
		Stats m_stats;
	};
};

namespace std {
	template <class Ty>
	void swap(memory::PoolAllocator<Ty>& left, memory::PoolAllocator<Ty>& right) noexcept {
		return left.swap(right);
	}
}
