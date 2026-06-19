import math
from typing import List

import mmh3


class BloomFilter:
    def __init__(self, size: int, hash_count: int) -> None:
        if size <= 0 or hash_count <= 0:
            raise ValueError("size and hash_count must be positive")

        self.size: int = size
        self.hash_count: int = hash_count
        self.bit_array: List[bool] = [False] * size
        self.items_count: int = 0

    @classmethod
    def from_expected(
        cls, expected_elements: int, false_positive_rate: float
    ) -> "BloomFilter":
        size = math.ceil(
            -(expected_elements * math.log(false_positive_rate)) / (math.log(2) ** 2)
        )
        hash_count = max(1, round((size / expected_elements) * math.log(2)))
        return cls(size=max(1, size), hash_count=max(1, hash_count))

    def _get_hashes(self, item: str) -> List[int]:
        indices = []
        item_bytes = str(item).encode()
        for i in range(self.hash_count):
            index = mmh3.hash(item_bytes, i) % self.size
            indices.append(index)
        return indices

    def add(self, item: str) -> None:
        is_new = False
        for index in self._get_hashes(item):
            if not self.bit_array[index]:
                is_new = True
                self.bit_array[index] = True
        if is_new:
            self.items_count += 1

    def contains(self, item: str) -> bool:
        for index in self._get_hashes(item):
            if not self.bit_array[index]:
                return False
        return True

    def __contains__(self, item: str) -> bool:
        return self.contains(item)

    @property
    def fill_ratio(self) -> float:
        filled = sum(1 for bit in self.bit_array if bit)
        return filled / self.size

    @property
    def current_false_positive_rate(self) -> float:
        if self.items_count == 0:
            return 0.0
        return (
            1.0 - math.exp(-self.hash_count * self.items_count / self.size)
        ) ** self.hash_count

    def stats(self) -> dict:
        return {
            "size": self.size,
            "hash_count": self.hash_count,
            "items_added": self.items_count,
            "fill_ratio": self.fill_ratio,
            "estimated_fp_rate": self.current_false_positive_rate,
        }


if __name__ == "__main__":
    bloom = BloomFilter.from_expected(expected_elements=1000, false_positive_rate=0.01)
    print(f"Оптимальный размер: {bloom.size}")
    print(f"Оптимальное количество хеш-функций: {bloom.hash_count}")

    words = ["apple", "banana", "orange", "grape", "kiwi", "mango", "peach"]
    for word in words:
        bloom.add(word)

    print(f"\n'watermelon' в фильтре: {'watermelon' in bloom}")
    print(f"Статистика: {bloom.stats()}")

    bloom2 = BloomFilter(size=20, hash_count=2)
    fruits = [
        "apple",
        "banana",
        "orange",
        "grape",
        "kiwi",
        "mango",
        "peach",
        "pear",
        "plum",
        "cherry",
    ]
    for fruit in fruits:
        bloom2.add(fruit)

    print(f"\nМаленький фильтр: 'watermelon' в фильтре: {'watermelon' in bloom2}")
    print(f"Статистика маленького фильтра: {bloom2.stats()}")
