#ifndef REF_COUNT_H_
#define REF_COUNT_H_

#include <atomic>
#include <cstddef>

class RefCount
{
public:
	RefCount() : mRefCount( 0 ) {}

	void AddRef() const
	{
		mRefCount.fetch_add( 1, std::memory_order_relaxed );
	}

	void Release() const
	{
		if ( mRefCount.fetch_sub( 1, std::memory_order_acq_rel ) == 1 )
		{
			onRefCountZero();
		}
	}

	// Drop one reference WITHOUT destroying the object when the count reaches
	// zero — returning it to the "floating" refcount-zero state a parser factory
	// hands to its caller (the caller's SmartPtr then takes ownership). Only
	// SmartPtr::release() uses this.
	void ReleaseNoDelete() const
	{
		mRefCount.fetch_sub( 1, std::memory_order_acq_rel );
	}

	int getRefCountForDebug() const { return mRefCount.load( std::memory_order_relaxed ); }

protected:
	virtual void onRefCountZero() const { delete this; }
	virtual ~RefCount() {}

private:
	mutable std::atomic<int> mRefCount;
};

template<class T>
class SmartPtr
{
public:
	SmartPtr() : mObj( nullptr ) {}
	SmartPtr( T *obj ) : mObj( obj ) { if ( mObj != nullptr ) mObj->AddRef(); }
	SmartPtr( const SmartPtr &ptr ) : mObj( ptr.mObj ) { if ( mObj != nullptr ) mObj->AddRef(); }
	~SmartPtr() { if ( mObj != nullptr ) mObj->Release(); }

	SmartPtr &operator=( const SmartPtr &ptr )
	{
		*this = ptr.mObj;
		return *this;
	}

	SmartPtr &operator=( T *obj )
	{
		if ( obj != nullptr )
		{
			obj->AddRef();
		}

		if ( mObj != nullptr )
			mObj->Release();
		mObj = obj;

		return *this;
	}

	bool operator==( const T *obj ) const { return ( mObj == obj ); }
	bool operator==( const SmartPtr &ptr ) const { return ( mObj == ptr.mObj ); }
	bool operator!=( const T *obj ) const { return ( mObj != obj ); }
	bool operator!=( const SmartPtr &ptr ) const { return ( mObj != ptr.mObj ); }

	T* operator->() { return mObj; }
	const T* operator->() const { return mObj; }

	operator T*() { return mObj; }
	operator const T*() const { return mObj; }

	const T* getObjectForDebug() const { return mObj; }

	// Give up this SmartPtr's reference and return the raw pointer WITHOUT
	// deleting the object (it may return to refcount zero — "floating"). Lets a
	// Parse factory guard its partially-built node with a SmartPtr (so a
	// CompileError thrown mid-parse frees it — KG-7) while still returning a raw,
	// unowned pointer on success, per the parser's factory convention.
	T *release()
	{
		T *obj = mObj;
		mObj = nullptr;
		if ( obj != nullptr )
			obj->ReleaseNoDelete();
		return obj;
	}

private:
	T *mObj;
};

#endif // REF_COUNT_H_
