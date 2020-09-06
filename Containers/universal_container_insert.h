#pragma once
#include <type_traits>
#include <utility>

namespace Algo::Container {
	template <class Container, class Ty>
	decltype(auto) insert(Container& cont, Ty&& value) {
		if constexpr (is_linear_v<Container>) {
			cont.push_back(std::forward<Ty>(value));
			return cont.back();
		}
		else if (is_associative_v<Container>) {
			return cont.insert(std::forward<Ty>(value));
		}
	}

	template <class Container, class Iterator, class Ty>
	decltype(auto) insert(Container& cont, Iterator where, Ty&& value) {
		return cont.insert(where, std::forward<Ty>(value));
	}
}