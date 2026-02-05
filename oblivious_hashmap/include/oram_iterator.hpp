#pragma once
#include <cstddef>
#include <iterator>
#include <concepts>

namespace ORAM
{
    // Forward declaration with matching template signature
    template <std::unsigned_integral IndexType, typename ValueType>
    class ObliviousRAM;

    template <std::unsigned_integral IndexType, typename ValueType>
    class ObliviousRAMIterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ValueType;
        using difference_type = std::ptrdiff_t;
        using pointer = ValueType *;
        using reference = ValueType &;

    private:
        ObliviousRAM<IndexType, ValueType> *oram;
        IndexType index;

    public:
        ObliviousRAMIterator() : oram(nullptr), index(0) {}
        ObliviousRAMIterator(ObliviousRAM<IndexType, ValueType> *oram, IndexType index)
            : oram(oram), index(index) {}

        reference operator*() const { return (*oram)[index]; }
        pointer operator->() const { return &(*oram)[index]; }

        ObliviousRAMIterator &operator++()
        {
            ++index;
            return *this;
        }

        ObliviousRAMIterator operator++(int)
        {
            ObliviousRAMIterator tmp = *this;
            ++index;
            return tmp;
        }

        ObliviousRAMIterator &operator--()
        {
            --index;
            return *this;
        }

        ObliviousRAMIterator operator--(int)
        {
            ObliviousRAMIterator tmp = *this;
            --index;
            return tmp;
        }

        ObliviousRAMIterator &operator+=(difference_type n)
        {
            index += n;
            return *this;
        }

        ObliviousRAMIterator &operator-=(difference_type n)
        {
            index -= n;
            return *this;
        }

        ObliviousRAMIterator operator+(difference_type n) const
        {
            return ObliviousRAMIterator(oram, index + n);
        }

        ObliviousRAMIterator operator-(difference_type n) const
        {
            return ObliviousRAMIterator(oram, index - n);
        }

        difference_type operator-(const ObliviousRAMIterator &other) const
        {
            return index - other.index;
        }

        reference operator[](difference_type n) const { return (*oram)[index + n]; }

        bool operator==(const ObliviousRAMIterator &other) const
        {
            return oram == other.oram && index == other.index;
        }

        bool operator!=(const ObliviousRAMIterator &other) const
        {
            return !(*this == other);
        }

        bool operator<(const ObliviousRAMIterator &other) const
        {
            return index < other.index;
        }

        bool operator<=(const ObliviousRAMIterator &other) const
        {
            return index <= other.index;
        }

        bool operator>(const ObliviousRAMIterator &other) const
        {
            return index > other.index;
        }

        bool operator>=(const ObliviousRAMIterator &other) const
        {
            return index >= other.index;
        }

        void reverse() { index = -index - 1; }
    };

    template <std::unsigned_integral IndexType, typename ValueType>
    ObliviousRAMIterator<IndexType, ValueType> operator+(
        typename ObliviousRAMIterator<IndexType, ValueType>::difference_type n,
        const ObliviousRAMIterator<IndexType, ValueType> &it)
    {
        return it + n;
    }

    template <std::unsigned_integral IndexType, typename ValueType>
    class ObliviousRAMIteratorReverse
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = ValueType;
        using difference_type = std::ptrdiff_t;
        using pointer = ValueType *;
        using reference = ValueType &;

    private:
        IndexType index;

    public:
        ObliviousRAMIteratorReverse() : index(0) {}
        ObliviousRAMIteratorReverse(IndexType index) : index(index) {}

        ObliviousRAMIteratorReverse &operator++()
        {
            --index;
            return *this;
        }

        ObliviousRAMIteratorReverse operator++(int)
        {
            ObliviousRAMIteratorReverse tmp = *this;
            --index;
            return tmp;
        }

        ObliviousRAMIteratorReverse &operator--()
        {
            ++index;
            return *this;
        }

        ObliviousRAMIteratorReverse operator--(int)
        {
            ObliviousRAMIteratorReverse tmp = *this;
            ++index;
            return tmp;
        }

        ObliviousRAMIteratorReverse &operator+=(difference_type n)
        {
            index -= n;
            return *this;
        }

        ObliviousRAMIteratorReverse &operator-=(difference_type n)
        {
            index += n;
            return *this;
        }

        ObliviousRAMIteratorReverse operator+(difference_type n) const
        {
            return ObliviousRAMIteratorReverse(index - n);
        }

        ObliviousRAMIteratorReverse operator-(difference_type n) const
        {
            return ObliviousRAMIteratorReverse(index + n);
        }

        difference_type operator-(const ObliviousRAMIteratorReverse &other) const
        {
            return other.index - index;
        }

        bool operator==(const ObliviousRAMIteratorReverse &other) const
        {
            return index == other.index;
        }

        bool operator!=(const ObliviousRAMIteratorReverse &other) const
        {
            return index != other.index;
        }

        bool operator<(const ObliviousRAMIteratorReverse &other) const
        {
            return index > other.index;
        }

        bool operator<=(const ObliviousRAMIteratorReverse &other) const
        {
            return index >= other.index;
        }

        bool operator>(const ObliviousRAMIteratorReverse &other) const
        {
            return index < other.index;
        }

        bool operator>=(const ObliviousRAMIteratorReverse &other) const
        {
            return index <= other.index;
        }

        void reverse() { index = -index - 1; }
    };
} // namespace ORAM
