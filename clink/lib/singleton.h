// Copyright (c) 2015 Martin Ridgers
// License: http://opensource.org/licenses/MIT

#pragma once

#include <assert.h>

//------------------------------------------------------------------------------
template <class T>
class singleton
{
public:
                singleton();
                ~singleton();
    static T*   get();

private:
    static T*&  get_store();
};

//------------------------------------------------------------------------------
template <class T> singleton<T>::singleton()
{
    assert(get_store() == nullptr);
    get_store() = (T*)this;
}

//------------------------------------------------------------------------------
template <class T> singleton<T>::~singleton()
{
    get_store() = nullptr;
}

//------------------------------------------------------------------------------
template <class T> T* singleton<T>::get()
{
    return get_store();
}

//------------------------------------------------------------------------------
template <class T> T*& singleton<T>::get_store()
{
    static T* instance;
    return instance;
}
