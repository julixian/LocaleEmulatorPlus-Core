#ifndef _TRAITS_H_0c85514c_9b05_4eaf_a9ae_8e46a810207c_
#define _TRAITS_H_0c85514c_9b05_4eaf_a9ae_8e46a810207c_

template<typename T>
struct TypeTraits
{
    typedef T           VALUE_TYPE;
    typedef T&          REF_TYPE;
    typedef const T&    CONST_REF_TYPE;
    VALUE_TYPE          value;
};

template<typename T>
struct TypeTraits<T&>
{
    typedef T           VALUE_TYPE;
    typedef T&          REF_TYPE;
    typedef const T&    CONST_REF_TYPE;
    VALUE_TYPE          value;
};

template<typename T>
struct TypeTraits<const T&>
{
    typedef T           VALUE_TYPE;
    typedef T&          REF_TYPE;
    typedef const T&    CONST_REF_TYPE;
    VALUE_TYPE          value;
};

template<>
struct TypeTraits<VOID>
{
    typedef VOID VALUE_TYPE;
    typedef VOID REF_TYPE;
    typedef VOID CONST_REF_TYPE;
};

#endif // _TRAITS_H_0c85514c_9b05_4eaf_a9ae_8e46a810207c_
#ifndef _OBJECT_H_fde9e5ea_50a7_46b2_aa60_bbe6492bedd4_
#define _OBJECT_H_fde9e5ea_50a7_46b2_aa60_bbe6492bedd4_


class ObjectBase
{
    ;
};

class Object : public ObjectBase
{
public:
    virtual ~Object() = 0 {}
};

#endif // _OBJECT_H_fde9e5ea_50a7_46b2_aa60_bbe6492bedd4_
#ifndef _POLICY_H_96a491a5_e296_46ff_8503_d35bff2cb7bb_
#define _POLICY_H_96a491a5_e296_46ff_8503_d35bff2cb7bb_


#endif // _POLICY_H_96a491a5_e296_46ff_8503_d35bff2cb7bb_
#ifndef _POINTER_H_6fac0398_48bd_494d_b268_63d42c5b7dc9_
#define _POINTER_H_6fac0398_48bd_494d_b268_63d42c5b7dc9_


template<typename PtrBase, typename PointerType>
class PointerImpl
{
protected:
    PULongPtr   RefCount;
    PointerType Reference;

protected:
    ULongPtr AddRef()
    {
        return RefCount != NULL ? ++*RefCount : 0;
    }

    ULongPtr Release()
    {
        return ((PtrBase *)this)->ReleaseWorker();
    }

    ULongPtr ReleaseWorker()
    {
        if (RefCount == NULL)
            return 0;

        ULongPtr RefCount = --*this->RefCount;

        if (RefCount == 0)
        {
            delete this->RefCount;

            ((PtrBase *)this)->ReleasePointer();

            RefCount = NULL;
            Reference = NULL;
        }

        return RefCount;
    }

    void ReleasePointer()
    {
        delete Reference;
    }

public:
    PointerImpl()
    {
        RefCount = NULL;
        Reference = NULL;
    }

    PointerImpl(PointerType Ptr)
    {
        (*this) = Ptr;
    }

    PointerImpl(const PointerImpl<PtrBase, PointerType>& Ptr)
    {
        (*this) = Ptr;
    }

    ~PointerImpl()
    {
        Release();
    }

    PtrBase& operator=(PointerType Ptr)
    {
        if (Ptr == Reference)
            return *((PtrBase *)this);

        Release();

        if (Ptr == NULL)
        {
            RefCount = NULL;
            Reference = NULL;
        }
        else
        {
            RefCount = new ULongPtr(1);
            Reference = Ptr;
        }

        return *((PtrBase *)this);
    }

    PtrBase& operator=(const PtrBase& Ptr)
    {
        if (this == &Ptr)
            return *((PtrBase *)this);

        Release();

        RefCount = Ptr.RefCount;
        Reference = Ptr.Reference;

        AddRef();

        return *((PtrBase *)this);
    }

    Bool operator== (const PointerType Ptr) const
    {
        return Reference == Ptr;
    }

    operator void*() const
    {
        return Reference;
    }

    Bool operator!= (const PointerType Ptr) const
    {
        return Reference != Ptr;
    }

    Bool operator> (const PointerType Ptr) const
    {
        return Reference > Ptr;
    }

    Bool operator< (const PointerType Ptr) const
    {
        return Reference < Ptr;
    }

    Bool operator>= (const PointerType Ptr) const
    {
        return Reference >= Ptr;
    }

    Bool operator<= (const PointerType Ptr) const
    {
        return Reference <= Ptr;
    }

    Bool operator!() const
    {
        return Reference != NULL;
    }

    operator bool() const
    {
        return Reference != NULL;
    }

    PointerType operator-> () const
    {
        return Reference;
    }
};

template<typename PointerType>
class Pointer : public PointerImpl<Pointer<PointerType>, PointerType>
{
public:
    const TYPE_OF(*((PointerType)0))& operator[](Int Index) const
    {
        return Reference[Index];
    }

    Pointer<PointerType>& operator=(PointerType Ptr)
    {
        return __super::operator=(Ptr);
    }

    Pointer<PointerType>& operator=(const Pointer<PointerType>& Ptr)
    {
        return __super::operator=(Ptr);
    }
};



#endif // _POINTER_H_6fac0398_48bd_494d_b268_63d42c5b7dc9_
#ifndef _SAFEWINTYPES_H_1bcf869c_5f0d_4e2b_9b04_212129cd7897_
#define _SAFEWINTYPES_H_1bcf869c_5f0d_4e2b_9b04_212129cd7897_


template<>
class Pointer<HANDLE> : public PointerImpl<Pointer<HANDLE>, HANDLE>
{
    friend class PointerImpl<Pointer<HANDLE>, HANDLE>;

    void ReleasePointer()
    {
        switch ((ULONG_PTR)Reference)
        {
            case (ULONG_PTR)NtCurrentProcess():
            case (ULONG_PTR)NtCurrentThread():
                return;
        }

        ZwClose(Reference);
    }

public:
    Pointer<HANDLE>& operator=(HANDLE Ptr)
    {
        return __super::operator=(Ptr);
    }

    Pointer<HANDLE>& operator=(const Pointer<HANDLE>& Ptr)
    {
        return __super::operator=(Ptr);
    }
};

typedef Pointer<HANDLE> Handle;


class FindHandle : public PointerImpl<FindHandle, HANDLE>
{
    friend class PointerImpl<FindHandle, HANDLE>;

    void ReleasePointer()
    {
        FindClose(Reference);
    }

public:
    FindHandle& operator=(HANDLE Ptr)
    {
        return __super::operator=(Ptr);
    }

    FindHandle& operator=(const FindHandle& Ptr)
    {
        return __super::operator=(Ptr);
    }
};


#endif // _SAFEWINTYPES_H_1bcf869c_5f0d_4e2b_9b04_212129cd7897_
