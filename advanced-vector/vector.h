#pragma once
#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <memory>
#include <type_traits>
#include <algorithm>

#include <iostream>

template <typename T>
class RawMemory {
public:
	RawMemory() = default;

	explicit RawMemory(size_t capacity)
		:buffer_{ Allocate(capacity) }
		, capacity_{ capacity } {
	}

	//конструкторы копирования удаляем
	RawMemory(const RawMemory&) = delete;
	RawMemory& operator = (const RawMemory& rhs) = delete;

	//конструкторы перемещения пишем
	RawMemory(RawMemory&& other) noexcept
		: buffer_{ other.buffer_ }
		, capacity_{ other.capacity_ } {
		other.buffer_ = nullptr;
		other.capacity_ = 0;
	}

	RawMemory& operator=(RawMemory&& rhs) noexcept {
		if (this != &rhs) {
			//нам без разницы что станет с правым, главное чтоб левый стал копией правого
			Swap(rhs);
		}
		return *this;
	}


	~RawMemory() {
		Deallocate(buffer_);
	}

	T* operator+(size_t offset) noexcept {
		// Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
		assert(offset <= capacity_);
		return buffer_ + offset;
	}

	const T* operator+(size_t offset) const noexcept {
		return const_cast<RawMemory&>(*this) + offset;
	}

	T& operator[](size_t index) noexcept {
		assert(index < capacity_);
		return buffer_[index];
	}

	const T& operator[](size_t index) const noexcept {
		return const_cast<RawMemory&>(*this)[index];
	}

	void Swap(RawMemory& other) noexcept {
		std::swap(buffer_, other.buffer_);
		std::swap(capacity_, other.capacity_);
	}

	const T* GetAddress() const noexcept {
		return buffer_;
	}

	T* GetAddress() noexcept {
		return buffer_;
	}

	size_t Capacity() const noexcept {
		return capacity_;
	}

private: //методы
	static T* Allocate(size_t n) {
		return n != 0 ? static_cast<T*>(operator new(sizeof(T) * n)) : nullptr;
	}

	static void Deallocate(T* buf) noexcept {
		operator delete(buf);
	}

private: //поля
	T* buffer_{};
	size_t capacity_{};
};

template <typename T>
class Vector {
public:

	using iterator = T*;
	using const_iterator = const T*;

	//Конструктор по умолчанию
	Vector() = default;

	//Конструктор вектора размером size с нулевыми значениями T
	explicit Vector(size_t size)
		: data_(size)
		, size_(size) {
		//заполняем с помощью value-инициализации
		std::uninitialized_value_construct_n(begin(), size_);
	}

	//конструктор копирования
	Vector(const Vector& other)
		//забронировать память размером как в other.size_
		:data_(other.size_)
		, size_(other.size_) {
		//копирование из other по кол-ву элементов
		std::uninitialized_copy_n(other.begin(), size_, begin());

	}

	Vector(Vector&& other) noexcept
		:data_{ std::move(other.data_) }
		, size_{ other.size_ } {
		other.size_ = 0;
	}

	Vector& operator=  (const Vector& rhs) {
		if (this != &rhs) {
			if (rhs.size_ > data_.Capacity()) {
				/* Применить copy-and-swap */
				Vector rhs_copy(rhs);
				Swap(rhs_copy);
			}
			else {
				//Размер вектора - источника меньше размера вектора - приёмника
				if (rhs.size_ < size_) {
					//копируем все элеиенты из rhs.data_	
					for (size_t i = 0; i < rhs.size_; ++i) {
						*(begin() + i) = *(rhs.begin() + i);
					}
					//не перезаписанные, а значит лишние, уничтожаем
					std::destroy_n(begin() + rhs.size_, size_ - rhs.size_);
				}//если больше или равен
				else {
					//тут минималка - размер this вектора, проходим по нему
					for (size_t i = 0; i < size_; ++i) {
						*(begin() + i) = *(rhs.begin() + i);
					}
					//а дальше копируем в свободную область
					std::uninitialized_copy((rhs.begin() + size_)
						, (rhs.end()), end());

				}
				size_ = rhs.size_;
			}
		}
		return *this;
	}

	Vector& operator= (Vector&& rhs) noexcept {
		if (this != &rhs) {
			//как и с RawMemory, нам без разницы что станет с правым объектом, 
			// главное чтоб левый стал им.
			Swap(rhs);
		}
		return *this;
	}

	void Swap(Vector& other) noexcept {
		data_.Swap(other.data_);
		std::swap(size_, other.size_);
	}

	//Деструктор 
	~Vector() {
		//стандартная функция удаления из памяти
		std::destroy_n(begin(), size_);
	}

	void Reserve(size_t new_capacity) {
		if (new_capacity < data_.Capacity()) {
			return;
		}
		RawMemory<T> new_data(new_capacity); //Если выбросит исключение то MyVector не изменится

		//Перемещайте элементы, только если соблюдается хотя бы одно из условий:
		// - конструктор перемещения типа T не выбрасывает исключений;
		// - тип T не имеет копирующего конструктора.

		//Шаблоны std::is_copy_constructible_v и std::is_nothrow_move_constructible_v 
		//помогают узнать, есть ли у типа копирующий конструктор и noexcept - конструктор 
		//перемещения.Выполняются эти шаблоны во время компиляции
		if constexpr (!std::is_copy_constructible_v<T> || std::is_nothrow_move_constructible_v<T>) {
			std::uninitialized_move_n(begin(), size_, new_data.GetAddress());
		}
		else {
			std::uninitialized_copy_n(begin(), size_, new_data.GetAddress());
		}

		//затем свапаем через метод RawData
		data_.Swap(new_data);
		//и избавляемся от улик
		std::destroy_n(new_data.GetAddress(), size_);
	}

	void Resize(size_t new_size) {
		//если уменьшаем
		if (new_size < size_) {
			//то удаляем элементы, у которых позиция больше нового размера
			for (size_t i = new_size; i < size_; ++i) {
				(begin() + i)->~T();
			}
		}//если увеличиваем
		else if (new_size > size_) {
			//бронируем больше памяти, если надо
			Reserve(new_size);
			//инициализируем новые объекты (T{})
			std::uninitialized_value_construct_n(end(), new_size - size_);
		}
		size_ = new_size;
	}


	void PushBack(const T& value) {

		EmplaceBack(value);
	}



	void PushBack(T&& value) {
		EmplaceBack(std::move(value));
	}

	template <typename... Types>
	T& EmplaceBack(Types&&... args) {
		if (size_ == Capacity()) {
			RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
			std::construct_at((new_data.GetAddress() + size_), std::forward<Types>(args)...);
			try {
				if constexpr (!std::is_copy_constructible_v<T> || std::is_nothrow_move_constructible_v<T>) {
					std::uninitialized_move_n(begin(), size_, new_data.GetAddress());
				}
				else {
					std::uninitialized_copy_n(begin(), size_, new_data.GetAddress());
				}
			}
			catch (...) {
				std::destroy_at(new_data.GetAddress() + size_);
				throw;
			}

			data_.Swap(new_data);
			std::destroy_n(new_data.GetAddress(), size_);
		}
		else {
			std::construct_at(end(), std::forward<Types>(args)...);
		}
		++size_;
		return *(end() - 1);
	}

	iterator Insert(const_iterator pos, const T& value) {
		return Emplace(pos, value);
	}

	iterator Insert(const_iterator pos, T&& value) {
		return Emplace(pos, std::move(value));
	}

	template <typename... Args>
	iterator Emplace(const_iterator pos, Args&&... args) {

		T* iter = const_cast<T*>(pos);

		//если места недостаточно
		if (size_ == Capacity()) {
			//выделяю новую память

			RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
			//чтобы получить место вставки в новой области памяти, 
			//надо найти отступ pos от begin() и с таким же отступом 
			//вставить в new_data  
			size_t index_from_pos = static_cast<size_t>(iter - begin());
			//std::cout << "index_from_pos = " << index_from_pos << std::endl;
			iter = new_data.GetAddress() + index_from_pos;
			//копирую в новую память
			//тут если что автоматически при неудаче удалится объект и бросится исключение
			//а RawMemory сам удалит память
			std::construct_at(iter, std::forward<Args>(args)...);
			try {
				if constexpr (!std::is_copy_constructible_v<T> || std::is_nothrow_move_constructible_v<T>) {
					//копирую элементы до pos
					std::uninitialized_move(begin(), const_cast<T*>(pos), new_data.GetAddress());
					//и после pos
					std::uninitialized_move(const_cast<T*>(pos), end(), iter + 1);
				}
				else {
					//копирую элементы до pos
					std::uninitialized_copy(begin(), const_cast<T*>(pos), new_data.GetAddress());
					//и после pos
					std::uninitialized_copy(const_cast<T*>(pos), end(), iter + 1);
				}
			}
			catch (...) {
				std::destroy_at(iter);
				throw;
			}
			data_.Swap(new_data);
			std::destroy_n(new_data.GetAddress(), size_);
			iter = begin() + index_from_pos;
		}//если места достаточно
		else {
			//Если вставка в конец или вектор пуст
			if (iter == end()) {
				std::construct_at(iter, std::forward<Args>(args)...);
				++size_;
				return iter;
			}
			else {
				//СЛУЧАЙ ВСТАВКИ В СЕРЕДИНУ
				//Создаем временный объект.
				T tmp(std::forward<Args>(args)...);
				//Перемещаем последний элемент в сырую память
				std::construct_at(end(), std::move(*(end() - 1)));
				//Сдвигаем диапазон вправо.
				std::move_backward(iter, end() - 1, end());
				//Записываем значение в позицию вставки через присваивание
				*iter = std::move(tmp);
			}
		}
		//увеличиваю размер
		++size_;
		return iter;
	}

	iterator Erase(const_iterator pos) {
		assert(pos >= begin() && pos < end());
		T* iter = const_cast<T*>(pos);
		std::move(iter + 1, end(), iter);
		--size_;
		std::destroy_at(end());
		return iter;
	}

	void PopBack() noexcept {
		--size_;
		std::destroy_at(end());
	}

	size_t Size() const noexcept {
		return size_;
	}

	size_t Capacity() const noexcept {
		return data_.Capacity();
	}

	const T& operator [](size_t index) const noexcept {
		return const_cast<Vector&>(*this)[index];
	}

	T& operator [](size_t index) noexcept {
		assert(index < size_);
		return data_[index];
	}

	iterator begin() noexcept {
		return data_.GetAddress();
	}

	iterator end() noexcept {
		return data_.GetAddress() + size_;
	}

	const_iterator begin() const noexcept {
		return data_.GetAddress();
	}

	const_iterator end() const noexcept {
		return data_.GetAddress() + size_;
	}

	const_iterator cbegin()const noexcept {
		return data_.GetAddress();
	}

	const_iterator cend()const noexcept {
		return data_.GetAddress() + size_;
	}

private: //приватные поля
	RawMemory<T> data_{};
	size_t size_{};
};