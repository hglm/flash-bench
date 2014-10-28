/*

Copyright (c) 2014 Harm Hanemaaijer <fgenfb@yahoo.com>

Permission to use, copy, modify, and/or distribute this software for any
purpose with or without fee is hereby granted, provided that the above
copyright notice and this permission notice appear in all copies.

THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*/

#include <stdint.h>
#include <math.h>

// Dynamic array template class allows any type of data.

template <class T>
class DynamicArray {
private :
	int nu_elements;
	int max_elements;
	int expansion_hint;
	T *data;

public :
	DynamicArray(int starting_capacity = 4) {
		nu_elements = 0;
		max_elements = 0;
		expansion_hint = starting_capacity;
	}
	inline int Size() const {
		return nu_elements;
	}
	inline T Get(int i) const {
		return data[i];
	}
	// By how much to expand the array the next time it is full.
	inline int GetExpansionHint(int size) {
		// Double the size each time.
		return size;
	}
	inline void ExpandCapacity() {
		max_elements += expansion_hint;
		data = (T *)realloc(data, sizeof(T) * max_elements);
		expansion_hint = GetExpansionHint(max_elements);
	}
	inline void TrimCapacity() {
		data = (T *)realloc(data, sizeof(T) * nu_elements);
		max_elements = nu_elements;
	}
	inline void Add(T v) {
		if (nu_elements == max_elements)
			ExpandCapacity();
		data[nu_elements] = v;
		nu_elements++;
	}
};

// Template class to cast DynamicArray class type to another, same-sized type.
// T1 is the new type, T2 is the same-sized type for which an existing class exists,
// and C2 is the name of the existing class.

template <class T1, class T2, class C2>
class CastDynamicArray : public C2 {
public :
	CastDynamicArray(int starting_capacity = 4) { }
	inline T1 Get(int i) const {
		return (T1)((C2 *)this->Get(i));
	}
	inline void Add(T1 s) {
		((C2 *)this)->Add((T2)s);
	}
};

typedef DynamicArray <int> IntArray;
typedef DynamicArray <int64_t> Int64Array;
#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit pointers.
typedef CastDynamicArray <void *, int, IntArray> PointerArray;
#else
// 64-bit pointers
typedef CastDynamicArray <void *, int64_t, Int64Array> PointerArray;
#endif
typedef CastDynamicArray <char *, void *, PointerArray> CharPointerArray;

template <class T>
class TightDynamicArray : public DynamicArray <T> {
public :
	TightDynamicArray(int starting_capacity = 4) { }
	// By how much to expand the array the next time it is full.
	inline int GetExpansionHint(int size) {
		// Conservatively expand the size of the array, keeping it tight.
		// Because processing speed is not likely to be critical for a tight array,
		// use some more expensive math functions.
		// A faster, integer log2 function could be used.
		float log2_size = log2f((float)size);
		int expansion = floorf(powf(1.5f, log2_size));
		// Size		Expand by
		// 1		1
		// 2		1
		// 4		2
		// 8		3
		// 32		7
		// 256   	25
		// 4096		129
		// 65536	656
		// 1000000	3234
		return expansion;
	}
};

typedef TightDynamicArray <int> TightIntArray;
typedef TightDynamicArray <int64_t> TightInt64Array;
#if UINTPTR_MAX == 0xFFFFFFFF
// 32-bit pointers.
typedef CastDynamicArray <void *, int, TightIntArray> TightPointerArray;
#else
// 64-bit pointers
typedef CastDynamicArray <void *, int64_t, TightInt64Array> TightPointerArray;
#endif
typedef CastDynamicArray <char *, void *, TightPointerArray> TightCharPointerArray;


